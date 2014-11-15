/*  Copyright (c) 2012-2012 Alexander Sabourenkov (screwdriver@lxnt.info)

    This software is provided 'as-is', without any express or implied
    warranty. In no event will the authors be held liable for any
    damages arising from the use of this software.

    Permission is granted to anyone to use this software for any
    purpose, including commercial applications, and to alter it and
    redistribute it freely, subject to the following restrictions:

    1. The origin of this software must not be misrepresented; you must
    not claim that you wrote the original software. If you use this
    software in a product, an acknowledgment in the product documentation
    would be appreciated but is not required.

    2. Altered source versions must be plainly marked as such, and
    must not be misrepresented as being the original software.

    3. This notice may not be removed or altered from any source
    distribution.
*/

#include <stdio.h>
#include <stdarg.h>

#include "zhban.h"

#include <uthash.h>
#include <utlist.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H

#include <hb.h>
#include <hb-ft.h>

const char *llname[] = { "fatal", "error", "warn ", "info ", "trace" };
static void printfsink(const int level, const char *fmt, va_list ap) {
    fprintf(stderr, "[%s] ", llname[level < 0 ? 0 : (level > 5 ? 5: level)]);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}

static void logrintf(int msg_level, int cur_level, logsink_t log_sink, const char *fmt, ...) {
    if (msg_level <= cur_level) {
        va_list ap;
        va_start(ap, fmt);
        log_sink(msg_level, fmt, ap);
        va_end(ap);
    }
}

#define log_trace(obj, fmt, args...) do { logrintf(ZHLOG_TRACE, (obj)->log_level, (obj)->log_sink, "%s(): " fmt, __func__, ## args); } while(0)
#define log_info(obj, fmt, args...)  do { logrintf(ZHLOG_INFO,  (obj)->log_level, (obj)->log_sink, fmt, ## args); } while(0)
#define log_warn(obj, fmt, args...)  do { logrintf(ZHLOG_WARN,  (obj)->log_level, (obj)->log_sink, fmt, ## args); } while(0)
#define log_error(obj, fmt, args...) do { logrintf(ZHLOG_ERROR, (obj)->log_level, (obj)->log_sink, fmt, ## args); } while(0)
#define log_fatal(obj, fmt, args...) do { logrintf(ZHLOG_FATAL, (obj)->log_level, (obj)->log_sink, fmt, ## args); } while(0)

typedef struct _spanner_baton {
    uint32_t *origin; // set to the glyph's origin. pixels are RG_16UI, so 32-bit. coords are GL/FT, not window (Y is up).
    uint32_t *first_pixel, *last_pixel; // bounds check
    uint16_t attribute; // per-glyph attribute (provoking character index ?)
    int32_t width; // width is in pixels.

    int min_span_x;
    int max_span_x;
    int min_y;
    int max_y;
    int fail;
    int log_level;
    logsink_t log_sink;
} spanner_baton_t;

static void spanner(int y, int count, const FT_Span* spans, void *user) {
    spanner_baton_t *baton = (spanner_baton_t *) user;

    if (y < baton->min_y)
        baton->min_y = y;
    if (y > baton->max_y)
        baton->max_y = y;

    uint32_t *scanline;
    for (int i = 0; i < count; i++) {
        if (spans[i].x + spans[i].len > baton->max_span_x)
            baton->max_span_x = spans[i].x + spans[i].len;

        if (spans[i].x < baton->min_span_x)
            baton->min_span_x = spans[i].x;

        if (baton->origin != NULL) {
            /* rendering */
            scanline = baton->origin + y * baton->width;
            if (scanline >= baton->first_pixel) {
                uint32_t *start = scanline + spans[i].x;
                uint16_t coverage = (spans[i].coverage<<8) | spans[i].coverage; // bit-replicate ala png
                uint16_t attribute = baton->attribute;

                if (start + spans[i].len < baton->last_pixel) {
                    for (int x = 0; x < spans[i].len; x++) {
                        // achtung: endianness, RMW
                        uint32_t t = *start & 0x0000FFFFU;
                        t |= (attribute << 16) | coverage;
                        *start++ = t;
                    }
                } else {
                    log_error(baton, "  error: span overflow");

                    log_info(baton, "  error: span overflow");
                    log_info(baton, "  span %d origin %p width %d (0x%x) scanline %p fp %p lp %p", i,
                                        baton->origin, baton->width, baton->width/4,
                                        scanline, baton->first_pixel, baton->last_pixel);
                    log_info(baton, "  span %d x=%d-%d y=%d start+len > last_pixel (%p + %d > %p).", i,
                                                        spans[i].x, spans[i].x + spans[i].len, y,
                                                           start, spans[i].len, baton->last_pixel);
                    baton->fail = 1;
                }
            } else {
                log_error(baton, "  error: scanline underflow");
                log_info(baton, "  span %d origin %p width %d (0x%x) scanline %p fp %p lp %p", i,
                        baton->origin, baton->width, baton->width/4, scanline, baton->first_pixel, baton->last_pixel);
                log_info(baton, "  span %d x=%d-%d y=%d scanline < first_pixel.", i,
                        spans[i].x, spans[i].x + spans[i].len, y);
                baton->fail = 1;
            }
        }
    }
}

static int force_ucs2_charmap(FT_Face ftf) {
    for(int i = 0; i < ftf->num_charmaps; i++)
        if ((  (ftf->charmaps[i]->platform_id == 0)
            && (ftf->charmaps[i]->encoding_id == 3))
           || ((ftf->charmaps[i]->platform_id == 3)
            && (ftf->charmaps[i]->encoding_id == 1)))
                return FT_Set_Charmap(ftf, ftf->charmaps[i]);
    return -1;
}

typedef struct _zhban_item {
    uint16_t *key; /* ucs-2 string*/
    uint32_t key_size; /* strsize */
    uint32_t key_allocd; /* might be > strsize if reused */

    zhban_rect_t texrect;
    uint32_t data_allocd; /* might be > w*h*4 if reused */

    UT_hash_handle hh;
    struct _zhban_item *prev;
    struct _zhban_item *next;
} zhban_item_t;

struct _half_zhban {
    int32_t log_level;
    logsink_t log_sink;

    int pixheight;
    int baseline_y; // well. let baseline be descent pixels above the bitmap bottom.

    FT_Library ft_lib;
    FT_Face ft_face;
    FT_Error ft_err;

    hb_font_t *hb_font;
    hb_buffer_t *hb_buffer;

    zhban_item_t *cache;
    zhban_item_t *history;

    uint32_t cache_size;
    uint32_t cache_limit;
    
    int32_t tab_step;
};

struct _zhban {
    struct _half_zhban sizer;
    struct _half_zhban render;
};

static int open_half_zhban(struct _half_zhban *half, const void *data, const uint32_t datalen,
                                        const uint32_t tab_step, int32_t loglevel, logsink_t logsink) {
    if ((half->ft_err = FT_Init_FreeType(&half->ft_lib)))
        return 1;

    if ((half->ft_err = FT_New_Memory_Face(half->ft_lib, data, datalen, 0, &half->ft_face)))
        return 1;

    if ((half->ft_err = force_ucs2_charmap(half->ft_face)))
        return half->ft_err;

    half->hb_font = hb_ft_font_create(half->ft_face, NULL);
    half->hb_buffer = hb_buffer_create();
    half->tab_step = tab_step;
    half->log_level = loglevel;
    half->log_sink = logsink;
    return 0;
}

static void drop_half_zhban(struct _half_zhban *half) {
    if (half->hb_buffer)
        hb_buffer_destroy(half->hb_buffer);
    if (half->hb_font)
        hb_font_destroy(half->hb_font);
    if (half->ft_face)
        FT_Done_Face(half->ft_face);
    if (half->ft_lib)
        FT_Done_FreeType(half->ft_lib);
}


zhban_t *zhban_open(const void *data, const uint32_t datalen, int pixheight, int tab_step,
                                        uint32_t sizerlimit, uint32_t renderlimit,
                                        int32_t loglevel, logsink_t logsink) {
    zhban_t *rv = malloc(sizeof(zhban_t));
    if (!rv)
        return rv;

    memset(rv, 0, sizeof(zhban_t));
    rv->sizer.cache_limit = sizerlimit;
    rv->render.cache_limit = renderlimit;
    if (   open_half_zhban(&rv->sizer, data, datalen, tab_step, loglevel, logsink ? logsink : printfsink)
        || open_half_zhban(&rv->render, data, datalen, tab_step, loglevel, logsink ? logsink : printfsink))
        goto error;

    FT_Size_RequestRec szreq;
    szreq.type = FT_SIZE_REQUEST_TYPE_SCALES; /* width and height are 16.16 scale values */
    szreq.horiResolution = szreq.vertResolution = 0; /* not used. */
    /*  scale value is 16.16 fixed point coefficient defined as FU_val*scale/0x10000 = pixel_val in 26.6
        (all those fixed point varieties bring madness)
        we must compute the scale value given the same height in pixels and font units.

          scale = pixheight / ft_face->height

        Note that FT_FaceRec::height is a line inteval, not max glyph height.
        So we use ascender-descender, not the height.

        Note that rounding can play bad games with pixel values,
        i. e. even if ascender-descender == height in font units,
        in pixels that may not hold true. */

    log_trace(&rv->sizer, "Font units: asc %08x dsc %08x h %08x delta %08x",
            rv->sizer.ft_face->ascender, - rv->sizer.ft_face->descender,
            rv->sizer.ft_face->height,
            rv->sizer.ft_face->ascender - rv->sizer.ft_face->descender);

    int req_size = pixheight, got_size, foo;;
    while(23) {
        szreq.width = ((req_size << 16) /
            (rv->sizer.ft_face->ascender - rv->sizer.ft_face->descender)) << 6;
        szreq.height = szreq.width;
        if ((rv->sizer.ft_err = FT_Request_Size(rv->sizer.ft_face, &szreq))) {
            /* error? hmm. keep trying */
            if (req_size > pixheight/2) {
                req_size -= 1;
                continue;
            }
            goto error;
        }
        got_size = (rv->sizer.ft_face->size->metrics.ascender
                        - rv->sizer.ft_face->size->metrics.descender) >> 6;
        foo = (rv->sizer.ft_face->size->metrics.ascender >> 6);
        foo -= (rv->sizer.ft_face->size->metrics.descender >> 6);
        log_trace(&rv->sizer, "sizereq %d, %d vs %d; h %ld", req_size, got_size, foo,
                                 rv->sizer.ft_face->size->metrics.height >> 6);
        if (got_size <= pixheight)
            break;
        req_size -= 1;
    }
    if ((rv->render.ft_err = FT_Request_Size(rv->render.ft_face, &szreq)))
        goto error;

    if (tab_step <= 0)
        tab_step = rv->sizer.ft_face->size->metrics.x_ppem;
    
    rv->sizer.baseline_y = - (rv->sizer.ft_face->size->metrics.descender >> 6);
    rv->render.baseline_y = rv->sizer.baseline_y;
    rv->sizer.pixheight = pixheight;
    rv->render.pixheight = pixheight;
    rv->sizer.tab_step = tab_step;
    rv->render.tab_step = tab_step;

    log_info(&rv->render, "render: asc %ld desc %ld height %ld x_ppem %d",
            rv->render.ft_face->size->metrics.ascender >> 6,
            rv->render.ft_face->size->metrics.descender >> 6,
            rv->render.ft_face->size->metrics.height >> 6,
            rv->render.ft_face->size->metrics.x_ppem);

    return rv;

    error:
    log_error(&rv->sizer, "zhban_open(): sizer FT_Err=0x%02X render FT_Err=0x%02X",
                                                rv->sizer.ft_err, rv->render.ft_err);
    drop_half_zhban(&rv->render);
    drop_half_zhban(&rv->sizer);
    free(rv);
    return NULL;
}

void zhban_drop(zhban_t *zh) {
    drop_half_zhban(&zh->sizer);
    drop_half_zhban(&zh->render);
    free(zh);
}

static uint16_t *utf16chr( uint16_t *hay, const uint16_t *haylimit, const uint16_t needle) {
    uint16_t *ptr = hay;
    while(haylimit - ptr > 0)
        if (*ptr == needle)
            return ptr;
        else
            ptr++;
    return ptr;
}

static void shape_stuff(struct _half_zhban *half, zhban_item_t *item) {
    spanner_baton_t stuffbaton;

    stuffbaton.log_level = half->log_level;
    stuffbaton.log_sink = half->log_sink;

    FT_Raster_Params ftr_params;
    ftr_params.target = 0;
    ftr_params.flags = FT_RASTER_FLAG_DIRECT | FT_RASTER_FLAG_AA;
    ftr_params.user = &stuffbaton;
    ftr_params.black_spans = 0;
    ftr_params.bit_set = 0;
    ftr_params.bit_test = 0;
    ftr_params.gray_spans = spanner;

    int x, y; // pen position.
    //int horizontal = HB_DIRECTION_IS_HORIZONTAL(hb_buffer_get_direction(half->hb_buffer));
    const int horizontal = 1;
    uint32_t *origin = item->texrect.data;
    int rendering = origin != NULL;

    int max_x = INT_MIN; // largest coordinate a pixel has been set at, or the pen was advanced to.
    int min_x = INT_MAX; // smallest coordinate a pixel has been set at, or the pen was advanced to.
    int max_y = INT_MIN; // this is max topside bearing along the string.
    int min_y = INT_MAX; // this is max value of (height - topbearing) along the string.
    /*  Naturally, the above comments swap their meaning between horizontal and vertical scripts,
        since the pen changes the axis it is advanced along.
        However, their differences still make up the bounding box for the string.
        Also note that all this is in FT coordinate system where y axis points upwards.
     */
    
    if (rendering) {
        memset(origin, 0, (item->texrect.w)*(item->texrect.h+1)*4);
        stuffbaton.width = item->texrect.w;
        stuffbaton.first_pixel = origin;
        stuffbaton.last_pixel = origin + item->texrect.w*item->texrect.h;
        stuffbaton.attribute = 0;
        /* origin of the first glyph - inital pen position, whatever. */
        if (horizontal) {
            //int offs = item->texrect.w * (item->texrect.h - item->texrect.baseline_offset - 1);
            origin = stuffbaton.first_pixel;// + offs - item->texrect.baseline_shift;
            item->texrect.cluster_map = item->texrect.data + item->texrect.w * item->texrect.h;
        } else {
            log_error(half, "error: non-horizontal scripts are not supported.");
        }
        /*  set initial pen position */
        x = item->texrect.origin_x;
        y = item->texrect.origin_y;

        log_trace(half, "renderering into %dx%d %d,%d", item->texrect.w, item->texrect.h, x, y);
    } else {
        /* disable rendering */
        stuffbaton.origin = NULL;
        x = 0;
        y = 0;
    }

    /* the idea is that the width of \t is (current_w extended to the next tabstop)-current_w 
        where current_w is ...
    
        so we shape the next substring up until the next '\t', then add the '\t' width
        to the current_x.
    */
    
    uint16_t *text = item->key;
    uint16_t *nextext = text;
    const uint16_t *textlimit = item->key + item->key_size/2;
    uint32_t textlen;
    int32_t cluster_offset; // number to add to glyph_info[j].cluster to arrive at correct value.
    log_trace(half, "initial text %p textlimit %p", text, textlimit);
    while (textlimit - nextext > 0) {
        nextext = utf16chr(text, textlimit, '\t');
        textlen = nextext - text;
        cluster_offset = text - item->key;
        log_trace(half, "sub: text %p nextext %p textlen %d cluoffs %d", text, nextext, textlen, cluster_offset);
        /* shaping a substring: its pointer is in text, its length in textlen (characters) */
        if (textlen > 0) {
            hb_buffer_clear_contents(half->hb_buffer);
            hb_buffer_set_direction(half->hb_buffer, HB_DIRECTION_LTR);
            hb_buffer_set_script(half->hb_buffer, HB_SCRIPT_LATIN);
            //hb_buffer_set_language(half->hb_buffer, hb_language_from_string("en", 2));
            hb_buffer_add_utf16(half->hb_buffer, text, textlen, 0, textlen);

            hb_shape(half->hb_font, half->hb_buffer, NULL, 0);

            uint32_t glyph_count;
            hb_glyph_info_t     *glyph_info = hb_buffer_get_glyph_infos(half->hb_buffer, &glyph_count);
            hb_glyph_position_t *glyph_pos  = hb_buffer_get_glyph_positions(half->hb_buffer, &glyph_count);
            FT_Error fterr;

            for (unsigned j = 0; j < glyph_count; ++j) {
                if ((fterr = FT_Load_Glyph(half->ft_face, glyph_info[j].codepoint, 0))) {
                    log_error(half, "FT_Load_Glyph(%08x): fterr=0x%02x",  glyph_info[j].codepoint, fterr);
                } else {
                    if (half->ft_face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) {
                        log_error(half, "unexpected glyph->format = %4s", (char *)&half->ft_face->glyph->format);
                    } else {
                        int gx = x + (glyph_pos[j].x_offset/64);
                        int gy = y + (glyph_pos[j].y_offset/64);

                        stuffbaton.min_span_x = INT_MAX;
                        stuffbaton.max_span_x = INT_MIN;
                        stuffbaton.min_y = INT_MAX;
                        stuffbaton.max_y = INT_MIN;
                        stuffbaton.fail = 0;

                        if (rendering) {
                            /* origin of the glyph. */
                            stuffbaton.origin = origin + stuffbaton.width * gy + gx;
                            stuffbaton.attribute = glyph_info[j].cluster + cluster_offset;
                        }

                        if ((fterr = FT_Outline_Render(half->ft_lib, &half->ft_face->glyph->outline, &ftr_params)))
                            log_error(half, "FT_Outline_Render() fterr=0x%02x", fterr);

                        if (stuffbaton.min_span_x != INT_MAX) {
                        /* Update values if the spanner was actually called. */
                            if (min_x > stuffbaton.min_span_x + gx)
                                min_x = stuffbaton.min_span_x + gx;

                            if (max_x < stuffbaton.max_span_x + gx)
                                max_x = stuffbaton.max_span_x + gx;

                            if (min_y > stuffbaton.min_y + gy)
                                min_y = stuffbaton.min_y + gy;

                            if (max_y < stuffbaton.max_y + gy)
                                max_y = stuffbaton.max_y + gy;
                        } else {
                        /* The spanner wasn't called at all - an empty glyph, like space. */
                            if (min_x > gx) min_x = gx;
                            if (max_x < gx) max_x = gx;
                            if (min_y > gy) min_y = gy;
                            if (max_y < gy) max_y = gy;
                        }
                        if (stuffbaton.fail) {
                            log_error(half, "fail at glyph %d cluster %d xy %d,%d gxy %d,%d x in [%d,%d] y in [%d,%d]",
                                    j, glyph_info[j].cluster, x, y, gx, gy,
                                    stuffbaton.min_span_x + gx, stuffbaton.max_span_x + gx,
                                    stuffbaton.min_y + gy, stuffbaton.max_y + gy
                                );
                        }
                    }
                }

                /* naive cluster map: just set from x+offset to x+offset+advance */
                if (rendering) {
                    int x_advance_px = glyph_pos[j].x_advance/64;
                    log_trace(half, "glyph %d cluster 0x%08x x %d advance %d", j, glyph_info[j].cluster, x, x_advance_px);
                    for (int cj = x; cj < x + x_advance_px; cj++)
                        if (cj < item->texrect.w)
                            item->texrect.cluster_map[cj] = glyph_info[j].cluster;
                        else
                            log_error(half, "cluster_map overflow by %d px", item->texrect.w - cj + 1);
                }

                x += glyph_pos[j].x_advance/64;
                y += glyph_pos[j].y_advance/64;
            }
        }
        /* eat any tabs */
        while ((textlimit - nextext > 0) && (*nextext == '\t')) {
            /* x+1 ensures  any '\t' exactly at a tab pos works. */
            int next_tab_pos = (x+1) % half->tab_step ? (x+1)/half->tab_step + 1 : (x+1)/half->tab_step;
            int tab_x_advance_px = next_tab_pos * half->tab_step - x;
            log_trace(half, "ate tab at %p [%d]: %d px (x=%d ntp %d)", nextext, (int)(textlimit - nextext),
                                                                    tab_x_advance_px, x, next_tab_pos);
            if (rendering) {
                int tab_character_index = nextext - item->key;
                for (int cj = x; cj < x + tab_x_advance_px; cj++)
                    if (cj < item->texrect.w)
                        item->texrect.cluster_map[cj] = tab_character_index;
                    else
                        log_error(half, "cluster_map overflow by %d px", item->texrect.w - cj + 1);
            }
            x += tab_x_advance_px;
            nextext += 1;
        }
        text = nextext;
    }
    if (1 || !rendering) { /* don't overwrite texrect values if we're rendering. */
        if (min_x > x) min_x = x;
        if (max_x < x) max_x = x;
        if (min_y > y) min_y = y;
        if (max_y < y) max_y = y;

        /* for horizontal :
            int baseline_offset = horizontal ? max_y : min_x;
            int baseline_shift = horizontal ? min_x : max_y;

            if baseline_offset is negative, we set origin_x to
                its negation to compensate at render
                and expand the width accordingly.
            if it is positive or zero, we set origin_x to zero.

            baseline_shift isn't really interesting since
            origin_y is defined in font face.
        */

        int tmp_w, ori_x;
        if (min_x < 0) {
            ori_x = -min_x;
            tmp_w = max_x - min_x;
        } else {
            tmp_w = max_x;
            ori_x = 0;
        }

        if (!rendering) {
            item->texrect.w = tmp_w;
            item->texrect.h = half->pixheight;
            item->texrect.origin_x = ori_x;
            item->texrect.origin_y = half->baseline_y;
        }

        log_error(half, "[%c] x [%d,%d] y [%d,%d] tmp_w=%d ori_x=%d tr = %d,%d %d,%d",
            rendering?'R':'S', min_x, max_x, min_y, max_y,
            tmp_w, ori_x,
            item->texrect.w, item->texrect.h,
            item->texrect.origin_x, item->texrect.origin_y);

        if (rendering && (tmp_w != item->texrect.w))
            log_error(half, "error: resetting item->texrect.w %d -> %d", item->texrect.w, tmp_w);
    }
}

static inline int item_sizeof(zhban_item_t *item) {
    return item ? sizeof(zhban_item_t) + item->key_allocd + item->data_allocd : 0;
}
/* returns an unused item, not it the hash or history list, ready to accept key of key_size and data of datasize bytes */
static zhban_item_t* get_usable_item(struct _half_zhban *hz, uint32_t key_size, uint32_t datasize) {
    zhban_item_t *item, *prev_item = NULL;

    while ((hz->cache_size > hz->cache_limit) && (hz->history != NULL)) {
        item = hz->history;
        HASH_DELETE(hh, hz->cache, item);
        DL_DELETE(hz->history, item);
        hz->cache_size -= item_sizeof(item);
        if (prev_item) {
            if (prev_item->key)
                free(prev_item->key);
            if (prev_item->texrect.data)
                free(prev_item->texrect.data);
            free(prev_item);
        }
        prev_item = item;
    }
    item = prev_item;
    if (!item) {
        item = (zhban_item_t *)malloc(sizeof(zhban_item_t));
        memset(item, 0, sizeof(zhban_item_t));
    }
    if (item->key_allocd < key_size) {
        if (item->key)
            item->key = realloc(item->key, key_size);
        else
            item->key = malloc(key_size);
    }
    if (item->data_allocd < datasize) {
        if (item->texrect.data)
            item->texrect.data = realloc(item->texrect.data, datasize);
        else
            item->texrect.data = malloc(datasize);
    }

    return item;
}

static int do_stuff(zhban_t *zhban, const uint16_t *string, const uint32_t strsize, int do_render, zhban_rect_t *rv) {
    zhban_item_t *item;
    struct _half_zhban *hz;
    uint32_t datasize;

    hz = do_render ? &zhban->render : &zhban->sizer;
    datasize = do_render ? rv->w * (rv->h + 1) * 4 : 0;

    HASH_FIND(hh, hz->cache, string, strsize, item);
    if (!item) {
        item = get_usable_item(hz, strsize, datasize);
        if (do_render) {
            /* copy geometry so we know where to render */
            item->texrect.w = rv->w;
            item->texrect.h = rv->h;
            item->texrect.origin_x = rv->origin_x;
            item->texrect.origin_y = rv->origin_y;
        } else {
            if (item->texrect.data != NULL) {
                log_error(hz, "error: sizer cache contamination!");
            }
        }

        /* copy the key */
        memcpy(item->key, string, strsize);
        item->key_size = strsize;

        shape_stuff(hz, item);
        HASH_ADD_KEYPTR(hh, hz->cache, item->key, item->key_size, item);
    } else {
        DL_DELETE(hz->history, item);
    }
    DL_APPEND(hz->history, item);
    memcpy(rv, &item->texrect, sizeof(zhban_rect_t));
    return 0;
}

int zhban_size(zhban_t *zhban, const uint16_t *string, const uint32_t strsize, zhban_rect_t *rv) {
    return do_stuff(zhban, string, strsize, 0, rv);
}

int zhban_render(zhban_t *zhban, const uint16_t *string, const uint32_t strsize, zhban_rect_t *rv) {
    return do_stuff(zhban, string, strsize, 1, rv);
}
