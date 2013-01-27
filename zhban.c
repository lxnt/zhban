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

#include "zhban.h"

#include <uthash.h>
#include <utlist.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H

#include <hb.h>
#include <hb-ft.h>

typedef struct _spanner_baton {
    uint32_t *origin; // set to the glyph's origin. pixels are RG_16UI, so 32-bit. coords are GL/FT, not window (Y is up).
    uint32_t *first_pixel, *last_pixel; // bounds check
    uint16_t attribute; // per-glyph attribute (provoking character index ?)
    uint32_t pitch; // pitch is in bytes.

    int min_span_x;
    int max_span_x;
    int min_y;
    int max_y;
} spanner_baton_t;

static void spanner(int y, int count, const FT_Span* spans, void *user) {
    spanner_baton_t *baton = (spanner_baton_t *) user;

    if (y < baton->min_y)
        baton->min_y = y;
    if (y > baton->max_y)
        baton->max_y = y;

    uint32_t rendering = baton->origin != NULL;
    uint32_t *scanline = baton->origin + y * ( (int) baton->pitch / 4 );

    for (int i = 0; i < count; i++) {
        if (spans[i].x + spans[i].len > baton->max_span_x)
            baton->max_span_x = spans[i].x + spans[i].len;

        if (spans[i].x < baton->min_span_x)
            baton->min_span_x = spans[i].x;

        if (rendering && scanline >= baton->first_pixel) {
            uint32_t *start = scanline + spans[i].x;
            uint16_t coverage = (spans[i].coverage<<8) | spans[i].coverage; // bit-replicate ala png
            uint16_t attribute = baton->attribute;

            if (start + spans[i].len < baton->last_pixel) {
                for (int x = 0; x < spans[i].len; x++) {
                    // achtung: endianness.
                    uint32_t t = *start & 0x0000FFFFU;
                    t |= (attribute << 16) | coverage;
                    *start++ = t;
                }
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
    uint32_t keysize; /* strsize */
    uint32_t keyallocd; /* might be > strsize if reused */

    zhban_rect_t texrect;
    uint32_t data_allocd; /* might be > w*h*4 if reused */

    UT_hash_handle hh;
    struct _zhban_item *prev;
    struct _zhban_item *next;
} zhban_item_t;

struct _half_zhban {
    FT_Library ft_lib;
    FT_Face ft_face;
    FT_Error ft_err;
    hb_font_t *hb_font;
    hb_buffer_t *hb_buffer;

    zhban_item_t *cache;
    zhban_item_t *history;

    uint32_t cache_size;
    uint32_t cache_limit;
};

struct _zhban {
    struct _half_zhban sizer;
    struct _half_zhban render;
    uint32_t glyph2ucs2[0x10000];
};

static int open_half_zhban(struct _half_zhban *half, const void *data, const uint32_t datalen) {
    if ((half->ft_err = FT_Init_FreeType(&half->ft_lib)))
        return 1;

    if ((half->ft_err = FT_New_Memory_Face(half->ft_lib, data, datalen, 0, &half->ft_face)))
        return 1;

    if ((half->ft_err = force_ucs2_charmap(half->ft_face)))
        return half->ft_err;

    half->hb_font = hb_ft_font_create(half->ft_face, NULL);
    half->hb_buffer = hb_buffer_create();
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

zhban_t *zhban_open(const void *data, const uint32_t datalen, int pixheight, uint32_t sizerlimit, uint32_t renderlimit) {
    zhban_t *rv = malloc(sizeof(zhban_t));
    if (!rv)
        return rv;

    memset(rv, 0, sizeof(zhban_t));
    rv->sizer.cache_limit = sizerlimit;
    rv->render.cache_limit = renderlimit;
    if (!open_half_zhban(&rv->sizer, data, datalen)) {
        if (!open_half_zhban(&rv->render, data, datalen)) {
            FT_Size_RequestRec szreq;
            szreq.type = FT_SIZE_REQUEST_TYPE_SCALES; /* width and height are 16.16 scale values */
            /*  scale value is 16.16 fixed point coefficient defined as FU_val*scale/0x10000 = pixel_val in 26.6
                (all those fixed point varieties bring madness)
                we must compute the scale value given the same height in pixels and font units, namely the line interval
                it's: scale = pixheight / ft_face->height
                computed as: */
            szreq.width = ((pixheight << 16) / rv->sizer.ft_face->height) << 6;
            szreq.height = szreq.width;
            szreq.horiResolution = szreq.vertResolution = 0; /* not used. */

            if (!(rv->sizer.ft_err = FT_Request_Size(rv->sizer.ft_face, &szreq)))
                if (!(rv->render.ft_err = FT_Request_Size(rv->render.ft_face, &szreq)))
                    return rv;
            drop_half_zhban(&rv->render);
        }
        drop_half_zhban(&rv->sizer);
    }
    free(rv);
    return NULL;
}

void zhban_drop(zhban_t *zh) {
    drop_half_zhban(&zh->sizer);
    drop_half_zhban(&zh->render);
    free(zh);
}

static void shape_stuff(struct _half_zhban *half, zhban_item_t *item) {
    hb_buffer_clear(half->hb_buffer);
    hb_buffer_set_direction(half->hb_buffer, HB_DIRECTION_LTR);
    hb_buffer_set_script(half->hb_buffer, HB_SCRIPT_LATIN);
    //hb_buffer_set_language(half->hb_buffer, hb_language_from_string("en", 2));
    hb_buffer_add_utf16(half->hb_buffer, item->key, item->keysize/2, 0, item->keysize/2);

    spanner_baton_t stuffbaton;

    FT_Raster_Params ftr_params;
    ftr_params.target = 0;
    ftr_params.flags = FT_RASTER_FLAG_DIRECT | FT_RASTER_FLAG_AA;
    ftr_params.user = &stuffbaton;
    ftr_params.black_spans = 0;
    ftr_params.bit_set = 0;

    ftr_params.bit_test = 0;
    ftr_params.gray_spans = spanner;

    int max_x = INT_MIN; // largest coordinate a pixel has been set at, or the pen was advanced to.
    int min_x = INT_MAX; // smallest coordinate a pixel has been set at, or the pen was advanced to.
    int max_y = INT_MIN; // this is max topside bearing along the string.
    int min_y = INT_MAX; // this is max value of (height - topbearing) along the string.
    /*  Naturally, the above comments swap their meaning between horizontal and vertical scripts,
        since the pen changes the axis it is advanced along.
        However, their differences still make up the bounding box for the string.
        Also note that all this is in FT coordinate system where y axis points upwards.
     */

    int x = 0;
    int y = 0;

    int horizontal = HB_DIRECTION_IS_HORIZONTAL(hb_buffer_get_direction(half->hb_buffer));
    uint32_t *origin = NULL;
    if (item->texrect.data) {
        stuffbaton.pitch = item->texrect.w * 4;
        stuffbaton.first_pixel = item->texrect.data;
        stuffbaton.last_pixel = item->texrect.data + item->texrect.w*item->texrect.h;
        stuffbaton.attribute = 0;
        /* achtung: wrong for vertical scripts */
        
        if (horizontal) {
            origin = stuffbaton.first_pixel + item->texrect.w * item->texrect.baseline_offset + item->texrect.baseline_shift; 
        } else {
            origin = stuffbaton.first_pixel + item->texrect.w * item->texrect.baseline_shift + item->texrect.baseline_offset; 
        }
    } else {
        /* disable rendering */
        stuffbaton.origin = NULL;
    }
    uint32_t glyph_count;
    hb_glyph_info_t     *glyph_info = hb_buffer_get_glyph_infos(half->hb_buffer, &glyph_count);
    hb_glyph_position_t *glyph_pos  = hb_buffer_get_glyph_positions(half->hb_buffer, &glyph_count);
    FT_Error fterr;
    for (unsigned j = 0; j < glyph_count; ++j) {
        if ((fterr = FT_Load_Glyph(half->ft_face, glyph_info[j].codepoint, 0))) {
            printf("load %08x failed fterr=%d.\n",  glyph_info[j].codepoint, fterr);
        } else {
            if (half->ft_face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) {
                printf("glyph->format = %4s\n", (char *)&half->ft_face->glyph->format);
            } else {
                int gx = x + (glyph_pos[j].x_offset/64);
                int gy = y + (glyph_pos[j].y_offset/64);

                stuffbaton.min_span_x = INT_MAX;
                stuffbaton.max_span_x = INT_MIN;
                stuffbaton.min_y = INT_MAX;
                stuffbaton.max_y = INT_MIN;

                if (origin) {
                    stuffbaton.origin = origin + gx + item->texrect.w * (item->texrect.h - gy);
                    stuffbaton.attribute = 0;
                }

                if ((fterr = FT_Outline_Render(half->ft_lib, &half->ft_face->glyph->outline, &ftr_params)))
                    printf("FT_Outline_Render() failed err=%d\n", fterr);

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
            }
        }

        x += glyph_pos[j].x_advance/64;
        y += glyph_pos[j].y_advance/64;
    }

    if (item->texrect.data == NULL) { /* don't overwrite texrect values if we're rendering. */
        if (min_x > x) min_x = x;
        if (max_x < x) max_x = x;
        if (min_y > y) min_y = y;
        if (max_y < y) max_y = y;

        item->texrect.w = max_x - min_x;
        item->texrect.h = max_y - min_y;

        if (horizontal) {
            item->texrect.baseline_offset = max_y;
            item->texrect.baseline_shift  = min_x;
        } else {
            item->texrect.baseline_offset = min_x;
            item->texrect.baseline_shift  = max_y;
        }
    }
}

static int do_stuff(zhban_t *zhban, const uint16_t *string, const uint32_t strsize, int do_render, zhban_rect_t *rv) {
    zhban_item_t *item;
    struct _half_zhban *hz;
    uint32_t itemsize;

    if (do_render) {
        hz = &zhban->sizer;
        itemsize = sizeof(zhban_item_t) + strsize;
    } else {
        hz = &zhban->render;
        itemsize = sizeof(zhban_item_t) + strsize + rv->w * rv->h * 4;
    }

    HASH_FIND(hh, hz->cache, string, strsize, item);
    if (!item) {
        /* decide if we add new or reuse LRU item */
        if (hz->cache_size + itemsize > hz->cache_limit) {
            item = hz->history;
            HASH_DELETE(hh, hz->cache, item);
            DL_DELETE(hz->history, item);
        } else {
            if (!(item = malloc(sizeof(zhban_item_t))))
                return 1;
            memset(item, 0, sizeof(zhban_item_t));
            hz->cache_size += sizeof(zhban_item_t);
        }

        /* [re]allocate buffers if needed (all sizes memset to 0 when adding new item) */
        if (item->keyallocd < strsize) {
            if (item->key)
                free(item->key);
            if (!(item->key = malloc(strsize))) {
                free(item);
                hz->cache_size -= sizeof(zhban_rect_t) + item->keyallocd;
                return 1;
            }
            hz->cache_size += strsize - item->keyallocd;
            item->keyallocd = strsize;
        }

        if (do_render) {
            if (item->data_allocd < rv->w*rv->h*4) {
                if (item->texrect.data)
                    free(item->texrect.data);
                uint32_t freed = item->data_allocd;
                item->data_allocd = rv->w*rv->h*4;
                if (!(item->texrect.data = malloc(item->data_allocd))) {
                    hz->cache_size -= sizeof(zhban_rect_t) + item->keyallocd + freed;
                    free(item->key);
                    free(item);
                    return 1;
                }
                hz->cache_size += item->data_allocd - freed;
            }
        } else {
            /* strictly necessary for documentation */
            item->texrect.data = NULL;
        }

        /* copy the key */
        memcpy(item->key, string, strsize);
        item->keysize = strsize;

        shape_stuff(hz, item);
        HASH_ADD_KEYPTR(hh, hz->cache, item->key, item->keysize, item);
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
