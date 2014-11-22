/*  Copyright (c) 2012-2014 Alexander Sabourenkov (screwdriver@lxnt.info)

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
#include <stddef.h>

#include "zhban.h"
#include "zhban-internal.h"

#include <uthash.h>
#include <utlist.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H

#include <hb.h>
#include <hb-ft.h>

#if defined(USE_SDL2)
#include "SDL.h"
typedef SDL_atomic_t refcount_t;
# define ZHBAN_INCREF(rc) (SDL_AtomicIncRef(&(rc)))
# if defined(__GNUC__)
/* lose -Wunused-value warning along with expression status */
#  define ZHBAN_DECREF(rc) do { int __attribute__ ((unused)) _urc = (SDL_AtomicDecRef(&(rc))); } while(0)
# else
#  define ZHBAN_DECREF(rc) (SDL_AtomicDecRef(&(rc)))
# endif
# define ZHBAN_GETREF(rc) (SDL_AtomicGet(&(rc)))
#else
typedef uint32_t refcount_t;
# define ZHBAN_INCREF(rc) ((rc)+=1)
# define ZHBAN_DECREF(rc) ((rc)-=1)
# define ZHBAN_GETREF(rc) ((rc))
#endif

//{ context

typedef struct _shape shape_t;
typedef struct _bitmap bitmap_t;
typedef struct _glyph glyph_t;

static void drop_shape_cache(shape_t **);
static void drop_bitmap_cache(bitmap_t **);
static void drop_glyph_cache(glyph_t **);
static void spanner(int px_y, int count, const FT_Span* spans, void *user);

typedef struct _zhban_internal {
    zhban_t outer;

    int32_t   log_level;
    logsink_t log_sink;

    uint32_t pixheight;
    uint32_t subpixel_positioning;  /* cache translated glyphs */

    FT_Library          ft_lib;
    FT_Face             ft_face;
    FT_Error            ft_err;
    FT_Raster_Params    ftr_params;

    hb_font_t      *hb_font;
    hb_buffer_t    *hb_buffer;
    hb_direction_t  hb_direction;
    hb_script_t     hb_script;
    hb_language_t   hb_language;

    /* shaped strings cache */
    shape_t *shaper_cache;
    shape_t *shaper_history;

    /* glyphs cache */
    glyph_t *glyph_cache;
    glyph_t *glyph_history;

    /* below - used in render thread */

    /* bitmap cache */
    bitmap_t *bitmap_cache;
    bitmap_t *bitmap_history;

} zhban_internal_t;

static int force_ucs2_charmap(FT_Face ftf) {
    for(int i = 0; i < ftf->num_charmaps; i++)
        if ((  (ftf->charmaps[i]->platform_id == 0)
            && (ftf->charmaps[i]->encoding_id == 3))
           || ((ftf->charmaps[i]->platform_id == 3)
            && (ftf->charmaps[i]->encoding_id == 1)))
                return FT_Set_Charmap(ftf, ftf->charmaps[i]);
    return -1;
}

zhban_t *zhban_open(const void *data, const uint32_t datalen, uint32_t pixheight,
                                        uint32_t subpx,
                                        uint32_t glyphlimit, uint32_t shaperlimit, uint32_t renderlimit,
                                        int32_t loglevel, logsink_t logsink) {

    zhban_internal_t *rv = malloc(sizeof(zhban_internal_t));
    if (!rv)
        return NULL;

    memset(rv, 0, sizeof(zhban_internal_t));
    rv->outer.glyph_limit = glyphlimit;
    rv->outer.shaper_limit = shaperlimit;
    rv->outer.bitmap_limit = renderlimit;

    rv->log_level = loglevel;
    rv->log_sink  = logsink ? logsink : printfsink;

    if ((rv->ft_err = FT_Init_FreeType(&rv->ft_lib)))
        goto error;

    if ((rv->ft_err = FT_New_Memory_Face(rv->ft_lib, data, datalen, 0, &rv->ft_face)))
        goto error;

    if ((rv->ft_err = force_ucs2_charmap(rv->ft_face)))
        goto error;

    rv->ftr_params.target = 0;
    rv->ftr_params.flags = FT_RASTER_FLAG_DIRECT | FT_RASTER_FLAG_AA;
    rv->ftr_params.user = NULL;
    rv->ftr_params.black_spans = 0;
    rv->ftr_params.bit_set = 0;
    rv->ftr_params.bit_test = 0;
    rv->ftr_params.gray_spans = spanner;

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

    log_trace(rv, "Font units: asc %08x dsc %08x h %08x delta %08x",
            rv->ft_face->ascender, - rv->ft_face->descender,
            rv->ft_face->height,
            rv->ft_face->ascender - rv->ft_face->descender);

    uint32_t req_size = pixheight, got_size;
    /* to rely on
            rv->shaper.ft_face->height
       or
            (rv->shaper.ft_face->ascender - rv->shaper.ft_face->descender)
       that is the question */
#if 0
    int foo;
    while(23) {
        szreq.width = ((req_size << 16) /
            (rv->ft_face->ascender - rv->ft_face->descender + 1)) << 6;
        szreq.height = szreq.width;
        if ((rv->ft_err = FT_Request_Size(rv->ft_face, &szreq))) {
            /* error? hmm. keep trying */
            if (req_size > pixheight/2) {
                req_size -= 1;
                continue;
            }
            goto error;
        }
        got_size = (rv->ft_face->size->metrics.ascender
                        - rv->ft_face->size->metrics.descender + 1) >> 6;

        /* foo is got_size calculated other way around to see any rounding errors */
        foo = (rv->ft_face->size->metrics.ascender >> 6);
        foo -= (rv->ft_face->size->metrics.descender >> 6);
        foo += 1;

        if (foo > got_size)
            got_size = foo;

        log_trace(rv, "req_size=%d, got_size=%d (%d); h=%ld", req_size, got_size, foo,
                                 rv->ft_face->size->metrics.height >> 6);
        if (got_size <= pixheight)
            break;
        req_size -= 1;
    }
#else
    while(42) {
        szreq.width = szreq.height = ((req_size << 16) / (rv->ft_face->height)) << 6;
        if ((rv->ft_err = FT_Request_Size(rv->ft_face, &szreq))) {
            /* error? hmm. keep trying */
            if (req_size > pixheight/2) {
                req_size -= 1;
                continue;
            }
            goto error;
        }
        got_size = rv->ft_face->size->metrics.height >> 6;
        log_trace(rv, "req_size=%d, got_size=%d", req_size, got_size);
        if (got_size <= pixheight)
            break;
        req_size -= 1;
    }
#endif

    rv->pixheight = pixheight;
    rv->subpixel_positioning = subpx;

    rv->outer.em_width = rv->ft_face->size->metrics.x_ppem;
    rv->outer.line_step = rv->ft_face->size->metrics.height >>6;

   if ((rv->ft_err = FT_Load_Char(rv->ft_face, 0x0020u, 0))) {
        log_error(rv, "FT_Load_Glyph(%08x): fterr=0x%02x", 0x0020u, rv->ft_err);
        goto error;
    }

    rv->outer.space_advance = rv->ft_face->glyph->linearHoriAdvance>>16;

    log_info(rv, "accepted metrics: asc %d desc %d height %d em_width %d space_advance %d pixheight %d",
            rv->ft_face->size->metrics.ascender >> 6,
            rv->ft_face->size->metrics.descender >> 6,
            rv->ft_face->size->metrics.height >> 6,

            rv->outer.em_width,
            rv->outer.line_step,
            rv->outer.space_advance,
            pixheight);

    /* initialize HB font here after font size is set (or ligatures go haywire) */
    rv->hb_font = hb_ft_font_create(rv->ft_face, NULL);
    rv->hb_buffer = hb_buffer_create();
    rv->hb_direction = HB_DIRECTION_LTR;
    rv->hb_script = HB_SCRIPT_INVALID;
    rv->hb_language = HB_LANGUAGE_INVALID;

    return (zhban_t *)rv;

    error:
    log_error(rv, "zhban_open(): FT_Err=0x%02X ", rv->ft_err);
    zhban_drop((zhban_t *)rv);
    free(rv);
    return NULL;
}

void zhban_set_script(zhban_t *zhban, const char *direction, const char *script, const char *language) {
    zhban_internal_t *z = (zhban_internal_t *) zhban;

    z->hb_direction = hb_direction_from_string(direction, direction ? strlen(direction) : 0);
    z->hb_script    = hb_script_from_string(script, script ? strlen(script) : 0);
    z->hb_language  = hb_language_from_string(language, language ? strlen(language) : 0);
}

void zhban_drop(zhban_t *zhban) {
    zhban_internal_t *z = (zhban_internal_t *) zhban;

    if (z->hb_buffer)
        hb_buffer_destroy(z->hb_buffer);
    if (z->hb_font)
        hb_font_destroy(z->hb_font);
    if (z->ft_face)
        FT_Done_Face(z->ft_face);
    if (z->ft_lib)
        FT_Done_FreeType(z->ft_lib);
    drop_bitmap_cache(&z->bitmap_cache); /* bitmaps first, as they reference shapes */
    drop_shape_cache(&z->shaper_cache);  /* then shapes, as they reference glyphs */
    drop_glyph_cache(&z->glyph_cache);

    free(z);
}

//}
//{ glyph_t
typedef struct _span {
     int16_t x, y;       /* FT does multiple spans per y value, we don't */
    uint16_t len;
    uint16_t coverage;  /* bit-replicate expanded from uint8_t */
} span_t;

typedef struct _glyph {
    /* key */
    uint32_t  codepoint;
    int32_t   frac_x; /* fractional part of translation */
    int32_t   frac_y; /* applied when this glyph was rendered */

    /* sizing section - in pixels. */
    int32_t min_span_x;
    int32_t max_span_x;
    int32_t min_y;
    int32_t max_y;

    uint32_t  spans_used;   /* bytes */
    uint32_t  spans_allocd; /* bytes */
    span_t   *spans;

    uint32_t  refcount;

    UT_hash_handle hh;
    struct _glyph *prev;
    struct _glyph *next;
} glyph_t;

static void add_glyph_spans(glyph_t *dst, const int32_t y, const FT_Span *spans, const uint32_t count) {
    const uint32_t required_bytes = count * sizeof(span_t);
    if (dst->spans_allocd - dst->spans_used < required_bytes ) {
        dst->spans_allocd += required_bytes;
        dst->spans = realloc(dst->spans, dst->spans_allocd);
    }
    for (uint32_t i = 0; i < count ; i++) {
        span_t *s = dst->spans + dst->spans_used/sizeof(span_t) + i;
        s->len = spans[i].len;
        s->x = spans[i].x;
        s->y = y;
        s->coverage = (spans[i].coverage << 8) | spans[i].coverage;
    }
    dst->spans_used += required_bytes;
}

static void drop_glyph(glyph_t *g) {
    free(g->spans);
    free(g);
}

static void drop_glyph_cache(glyph_t **head) {
    glyph_t *elt, *tmp;
    HASH_ITER(hh, *head, elt, tmp) {
        HASH_DEL(*head, elt);
        drop_glyph(elt);
    }
}

static inline uint32_t glyph_sizeof(glyph_t *glyph) {
    return sizeof(glyph_t) + glyph->spans_allocd;
}

static inline uint32_t glyph_expected_spans(zhban_internal_t *z) {
    return (z->outer.glyph_spans_seen ? z->outer.glyph_spans_seen : 1) /
           (z->outer.glyph_gets ? z->outer.glyph_gets : 1)  + 1;
}
static inline uint32_t glyph_expected_sizeof(zhban_internal_t *z) {
    return sizeof(glyph_t) + sizeof(span_t) * glyph_expected_spans(z);
}

static glyph_t *reallocate_glyph(zhban_internal_t *z, glyph_t *glyph) {
    if (!glyph) {
        glyph = malloc(sizeof(glyph_t));
        memset(glyph, 0, sizeof(glyph_t));
    }
    if (glyph->spans_allocd < sizeof(span_t) * glyph_expected_spans(z)) {
        glyph->spans_allocd = sizeof(span_t) * glyph_expected_spans(z);
        glyph->spans = malloc(glyph->spans_allocd);
    }
    return glyph;
}

static glyph_t *get_idle_glyph(zhban_internal_t *z) {
    glyph_t *item, *evicted_item = NULL, *tmp;
    uint32_t needed_space = glyph_expected_sizeof(z);
    log_trace(z, "need %d have %d (%d - %d)", needed_space,
            z->outer.glyph_limit - z->outer.glyph_size, z->outer.glyph_limit, z->outer.glyph_size);

    /* if we are over the cache size limit, clean up some. */
    DL_FOREACH_SAFE(z->glyph_history, item, tmp) {
        /* ignore referenced ones */
        if (item->refcount)
            continue;

        /* if we have enough space at last .. */
        if (needed_space < (z->outer.glyph_limit - z->outer.glyph_size)) {
            if (evicted_item)
                /* got it by eviction, reuse item so as to not do free/alloc dance */
                item = evicted_item;
            else
                /* just have free space here; reallocate_shape() will allocate new item */
                item = NULL;
            break;
        }

        /* drop evicted item if we need to evict more that one */
        if (evicted_item)
            drop_glyph(evicted_item);

        HASH_DELETE(hh, z->glyph_cache, item);
        DL_DELETE(z->glyph_history, item);
        z->outer.glyph_size -= glyph_sizeof(item);
        z->outer.glyph_evictions += 1;
        evicted_item = item;
    }

    /* end up here with either an item to be reused (storage not touched),
       or with item == NULL, in case either there's space in the cache to use,
       or we failed to free up space in the cache (like, too much shapes with
       refcount > 0), in which case we ignore the cache size limit. */

    glyph_t *rv = reallocate_glyph(z, item);

    /* grow cache to avoid thrashing (?) */
    if (z->outer.glyph_limit < z->outer.glyph_size) {
        log_trace(z, "grew glyph cache from %d to %d", z->outer.glyph_limit, z->outer.glyph_size);
        z->outer.glyph_limit = z->outer.glyph_size;
    }

    return rv;
}

static void spanner(int y, int count, const FT_Span* spans, void *user) {
    glyph_t *glyph = (glyph_t *) user;

    if (y < glyph->min_y)
        glyph->min_y = y;
    if (y > glyph->max_y)
        glyph->max_y = y;

    for (int i = 0; i < count; i++) {
        int32_t max_x = (spans[i].x + spans[i].len);
        int32_t min_x = (spans[i].x);
        if ( max_x > glyph->max_span_x)
            glyph->max_span_x = max_x;
        if (min_x < glyph->min_span_x)
            glyph->min_span_x = min_x;
    }
    add_glyph_spans(glyph, y, spans, count);
}
/* returns nonzero on error */
static int render_glyph(zhban_internal_t *z, glyph_t *glyph) {
   if ((z->ft_err = FT_Load_Glyph(z->ft_face, glyph->codepoint, 0))) {
        log_error(z, "FT_Load_Glyph(%08x): fterr=0x%02x", glyph->codepoint, z->ft_err);
        return 1;
    }
    if (z->ft_face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) {
        log_error(z, "unexpected glyph->format = %4s", (char *)&z->ft_face->glyph->format);
        return 1;
    }
    /* translate by the fractional part of the offset
       not putting if (z->subpixel_positioning) here since FT_Outline_Translate()
       is supposed to be cheap. */
    FT_Outline_Translate(&z->ft_face->glyph->outline, glyph->frac_x, glyph->frac_y);

    glyph->min_span_x = INT_MAX;
    glyph->max_span_x = INT_MIN;
    glyph->min_y = INT_MAX;
    glyph->max_y = INT_MIN;
    glyph->spans_used = 0;

    z->ftr_params.user = glyph;

    if ((z->ft_err = FT_Outline_Render(z->ft_lib, &z->ft_face->glyph->outline, &z->ftr_params))) {
        log_error(z, "FT_Outline_Render() fterr=0x%02x", z->ft_err);
        goto error;
    }

    z->outer.glyph_rendered += 1;
    z->outer.glyph_spans_seen += glyph->spans_used/sizeof(span_t);

    FT_Outline_Translate(&z->ft_face->glyph->outline, -glyph->frac_x, -glyph->frac_y);

    log_trace(z, "cp %x %d spans frac_xy %d, %d, minmax_x %d, %d", glyph->codepoint,
        glyph->spans_used/sizeof(span_t),
        glyph->frac_x, glyph->frac_y, glyph->min_span_x, glyph->max_span_x);

    return 0;
error:
    return 1;
}

static glyph_t *get_a_glyph(zhban_internal_t *z, uint32_t codepoint, int32_t frac_x, int32_t frac_y) {
    glyph_t *item;
    const int keylen = 3 * sizeof(int32_t);

    if (z->subpixel_positioning) {
        glyph_t key;
        key.codepoint = codepoint;
        key.frac_x = frac_x;
        key.frac_y = frac_y;
        HASH_FIND(hh, z->glyph_cache, &key, keylen, item);
    } else {
        HASH_FIND_INT(z->glyph_cache, &codepoint, item);
    }

    z->outer.glyph_gets += 1;
    if (item) {
        /* put the item at the head of history list*/
        DL_DELETE(z->glyph_history, item);
        DL_APPEND(z->glyph_history, item);
        z->outer.glyph_hits += 1;
        return item;
    }

    item = get_idle_glyph(z);
    item->codepoint = codepoint;
    item->frac_x = frac_x;
    item->frac_y = frac_y;

    /* suboptimally drop a glyph if rendering failed. */
    /* it's that, or keep a list of them.. since it's very
       rare to fail here, just drop it */
    if (render_glyph(z, item)) {
        drop_glyph(item);
        return NULL;
    }

    if (z->subpixel_positioning) {
        HASH_ADD_KEYPTR(hh, z->glyph_cache, item, keylen, item);
    } else {
        HASH_ADD_INT(z->glyph_cache, codepoint, item);
    }
    DL_APPEND(z->glyph_history, item);
    z->outer.glyph_size += glyph_sizeof(item);

    return item;
}
//}
//{ shape_t
/* glyph rendering sequence item. */
typedef struct _glyph_info {
    glyph_t  *glyph;        /* points into glyph cache */
    int32_t   x_origin;     /* in 26.6 pixels */
    int32_t   y_origin;     /* in 26.6 pixels */
    uint32_t  cluster;
}   glyph_info_t;

typedef struct _shape {
    zhban_shape_t shape;

    refcount_t refcount;

    /* USC-2 string, also hash key */
    uint16_t *key;
    uint32_t key_size;      /* size in bytes */
    uint32_t key_allocd;    /* might be > size if reused */

    /* shaper results */
    glyph_info_t *glyphs;
    uint32_t glyphs_allocd; /* in bytes. see add_glyph_info() */
    uint32_t glyphs_used;   /* in bytes. */

    UT_hash_handle hh;
    struct _shape *prev;
    struct _shape *next;
} shape_t;

static inline uint32_t expected_glyph_count(const uint32_t key_size) {
    /* FIXME: ? */
    return 3*key_size/4;
}

static uint32_t shape_sizeof(const shape_t *p) {
    return sizeof(shape_t) + p->key_allocd + p->glyphs_allocd;
}

static uint32_t shape_expected_sizeof(const uint32_t key_size) {
    return sizeof(shape_t) + key_size + sizeof(glyph_info_t) * expected_glyph_count(key_size);
}

static shape_t *reallocate_shape(shape_t *shape, const uint32_t key_size) {
    if (!shape) {
        shape = malloc(sizeof(shape_t));
        memset(shape, 0, sizeof(shape_t));
    }
    if (shape->key_allocd < key_size) {
        shape->key = realloc(shape->key, key_size);
        shape->key_allocd = key_size;
    }
    if (shape->glyphs_used) {
        for (uint32_t i=0; i < shape->glyphs_used/sizeof(glyph_info_t); i++)
            shape->glyphs[i].glyph->refcount -= 1;
        shape->glyphs_used = 0;
    }
    if (shape->glyphs_allocd < sizeof(glyph_info_t) * expected_glyph_count(key_size)) {
        shape->glyphs_allocd = sizeof(glyph_info_t) * expected_glyph_count(key_size);
        shape->glyphs = realloc(shape->glyphs, shape->glyphs_allocd);
    }
    return shape;
}

static void drop_shape(shape_t *s) {
    for (uint32_t i=0; i < s->glyphs_used/sizeof(glyph_info_t); i++)
        s->glyphs[i].glyph->refcount -= 1;
    free(s->key);
    free(s->glyphs);
    free(s);
}

static void drop_shape_cache(shape_t **head) {
    shape_t *elt, *tmp;
    HASH_ITER(hh, *head, elt, tmp) {
        HASH_DEL(*head, elt);
        drop_shape(elt);
    }
}

static shape_t *get_idle_shape(zhban_internal_t *z, const uint32_t key_size) {
    shape_t *item, *evicted_item = NULL, *tmp;
    uint32_t needed_space = shape_expected_sizeof(key_size);
    log_trace(z, "need %d have %d (%d - %d)", needed_space,
        z->outer.shaper_limit - z->outer.shaper_size, z->outer.shaper_limit, z->outer.shaper_size);

    /* if we are over the cache size limit, clean up some. */
    DL_FOREACH_SAFE(z->shaper_history, item, tmp) {
        /* ignore referenced ones */

        if (ZHBAN_GETREF(item->refcount))
            continue;

        /* if we have enough space at last .. */
        if (needed_space < (z->outer.shaper_limit - z->outer.shaper_size)) {
            if (evicted_item)
                /* got it by eviction, reuse item so as to not do free/alloc dance */
                item = evicted_item;
            else
                /* just have free space here; reallocate_shape() will allocate new item */
                item = NULL;
            break;
        }

        /* drop evicted item if we need to evict more that one */
        if(evicted_item)
            drop_shape(evicted_item);

        HASH_DELETE(hh, z->shaper_cache, item);
        DL_DELETE(z->shaper_history, item);
        z->outer.shaper_size -= shape_sizeof(item);
        z->outer.shaper_evictions += 1;
        evicted_item = item;
    }

    /* end up here with either an item to be reused (storage not touched),
       or with item == NULL, in case either there's space in the cache to use,
       or we failed to free up space in the cache (like, too much shapes with
       refcount > 0), in which case we ignore the cache size limit. */

    shape_t *rv = reallocate_shape(item, key_size);

    /* grow cache to avoid thrashing (?) */
    if (z->outer.shaper_limit < z->outer.shaper_size) {
        log_trace(z, "grew cache from %d to %d", z->outer.shaper_limit, z->outer.shaper_size);
        z->outer.shaper_limit = z->outer.shaper_size;
    }

    return rv;
}

static void add_glyph_info(shape_t *dst, glyph_t *glyph, int32_t x_origin, int32_t y_origin, uint32_t cluster) {
    if (dst->glyphs_used == dst->glyphs_allocd) {
        /* reallocate with some space (25%+2 more glyphs) to spare */
        dst->glyphs_allocd += dst->glyphs_allocd / 4 + sizeof(glyph_info_t) * 2;
        dst->glyphs = realloc(dst->glyphs, dst->glyphs_allocd);
    }
    int i = dst->glyphs_used/sizeof(glyph_info_t);
    dst->glyphs[i].glyph = glyph;
    glyph->refcount += 1;
    dst->glyphs[i].x_origin = x_origin;
    dst->glyphs[i].y_origin = y_origin;
    dst->glyphs[i].cluster  = cluster;
    dst->glyphs_used += sizeof(glyph_info_t);
}

static void adjust_glyph_origin(shape_t *dst, hb_position_t delta_x, hb_position_t delta_y) {
    for (uint32_t i = 0; i < dst->glyphs_used / sizeof(glyph_info_t); i++) {
        dst->glyphs[i].x_origin += delta_x;
        dst->glyphs[i].y_origin += delta_y;
    }
}
//}
//{ shaper
static inline int32_t grid_fit_266(int32_t value) {
    return (value > 0) ?
            ((value>>6) + (value & 0x3f ? 1 : 0)) :
            ((value>>6) - (value & 0x3f ? 1 : 0)) ;
}

static void shape_string(zhban_internal_t *z, shape_t *item) {
    int x = 0, y = 0; // pen position, FT 26.6
    //int horizontal = HB_DIRECTION_IS_HORIZONTAL(hb_buffer_get_direction(z->hb_buffer));

    int max_x = INT_MIN; // largest coordinate a pixel has been set at, or the pen was advanced to.
    int min_x = INT_MAX; // smallest coordinate a pixel has been set at, or the pen was advanced to.
    int max_y = INT_MIN; // this is max topside bearing along the string.
    int min_y = INT_MAX; // this is max value of (height - topbearing) along the string.
    /*  Naturally, the above comments swap their meaning between horizontal and vertical scripts,
        since the pen changes the axis it is advanced along.
        However, their differences still make up the bounding box for the string.
        Also note that all this is in FT coordinate system where y axis points upwards.
     */

    item->glyphs_used = 0; // reset glyph info/position storage

    hb_buffer_clear_contents(z->hb_buffer);
    hb_buffer_set_direction(z->hb_buffer, z->hb_direction);
    hb_buffer_set_script(z->hb_buffer, z->hb_script);
    hb_buffer_set_language(z->hb_buffer, z->hb_language);
    hb_buffer_add_utf16(z->hb_buffer, item->key, item->key_size/2, 0, item->key_size/2);

    hb_shape(z->hb_font, z->hb_buffer, NULL, 0);

    uint32_t glyph_count;
    hb_glyph_info_t     *glyph_info = hb_buffer_get_glyph_infos(z->hb_buffer, &glyph_count);
    hb_glyph_position_t *glyph_pos  = hb_buffer_get_glyph_positions(z->hb_buffer, &glyph_count);

    for (uint32_t j = 0; j < glyph_count; ++j) {
        int32_t gx = x + glyph_pos[j].x_offset;
        int32_t gy = y + glyph_pos[j].y_offset;
        glyph_t *glyph = get_a_glyph(z, glyph_info[j].codepoint, gx & 0x3f, gy & 0x3f);
        if (glyph) {
            if (glyph->min_span_x != INT_MAX) {
            /* Update values if the spanner was actually called. */
                if (min_x > (glyph->min_span_x<<6) + gx)
                    min_x = (glyph->min_span_x<<6) + gx;

                if (max_x < (glyph->max_span_x<<6) + gx)
                    max_x = (glyph->max_span_x<<6) + gx;

                if (min_y > (glyph->min_y<<6) + gy)
                    min_y = (glyph->min_y<<6) + gy;

                if (max_y < (glyph->max_y<<6) + gy)
                    max_y = (glyph->max_y<<6) + gy;
            } else {
            /* The spanner wasn't called at all - an empty glyph, like space. */
                if (min_x > gx) min_x = gx;
                if (max_x < gx) max_x = gx;
                if (min_y > gy) min_y = gy;
                if (max_y < gy) max_y = gy;
                log_trace(z, "glyph %d: empty.", j); /* can't skip rendering it though? */
            }
            add_glyph_info(item, glyph, gx, gy, glyph_info[j].cluster);
            log_trace(z, "glyph %d at %d.%d, %d.%d", j,
                gx>>6, 100*abs(gx&0x3f)/64, gy>>6, 100*abs(gy&0x3f)/64);
        } /* else render_glyph() failed, skip it */
        x += glyph_pos[j].x_advance;
        y += glyph_pos[j].y_advance;
    }

    if (min_x > x) min_x = x;
    if (max_x < x) max_x = x;
    if (min_y > y) min_y = y;
    if (max_y < y) max_y = y;

    log_trace(z, "extents: x [%d.%d, %d.%d] y [%d.%d, %d.%d]",
            min_x>>6, 100*abs(min_x &0x3f)/64, max_x>>6, 100*abs(max_x &0x3f)/64,
            min_y>>6, 100*abs(min_y & 0x3f)/64, max_y>>6, 100*abs(max_y & 0x3f)/64);

    int32_t origin_x, origin_y, w, h;  /* in fp26.6 */

    if (min_x <= 0) {
        /* means some pixels ended up with negative x coordinate. set up origin translation
           so that they all end up with nonzero ones */
        origin_x = -min_x;
        w = max_x - min_x + 0x40;
    } else {
        /* all pixels to the right of origin. it happens in regular fonts. cut down the unused space.
           so that spacing isn't broken */
        origin_x = -min_x;
        w = max_x - min_x;
    }

    if (min_y <= 0) {
        /* means some pixels ended up with negative y coordinate. this is very common, origin
           being at the baseline, thus descenders are always below.
           set up origin translation so that all pixels end up with nonzero y-coord. */
        origin_y = -min_y;
        h = max_y - min_y + 0x40;
    } else {
        /* all pixels above the baseline. no idea why (all superscript?), but cut down the unused space.*/
        origin_y = -min_y;
        h = max_y - min_y;
    }

    /* adjust glyph origins - effectively move (0,0) around so that all pixels fit into the bitmap */
    adjust_glyph_origin(item, origin_x, origin_y);

    log_trace(z, "26.6 w,h =  %d.%d, %d.%d origin = %d.%d, %d.%d",
                w>>6, 100*abs(w&0x3f)/64, h>>6, 100*abs(h&0x3f)/64,
                origin_x>>6, 100*abs(origin_x&0x3f)/64, origin_y>>6, 100*abs(origin_y&0x3f)/64);

    /* grid fit */
    item->shape.w = grid_fit_266(w);
    item->shape.h = grid_fit_266(h);
    item->shape.origin_x = grid_fit_266(origin_x);
    item->shape.origin_y = grid_fit_266(origin_y);
    log_trace(z, "FITD w,h =  %d,%d origin = %d,%d shape %p",
        item->shape.w, item->shape.h, item->shape.origin_x, item->shape.origin_y, item);
}

zhban_shape_t *zhban_shape(zhban_t *zhban, const uint16_t *string, const uint32_t strsize) {
    zhban_internal_t *z = (zhban_internal_t *)zhban;
    shape_t *item;

    z->outer.shaper_gets += 1;
    HASH_FIND(hh, z->shaper_cache, string, strsize, item);
    if (item) {
        /* put the item at the head of history list*/
        DL_DELETE(z->shaper_history, item);
        DL_APPEND(z->shaper_history, item);
        ZHBAN_INCREF(item->refcount);
        z->outer.shaper_hits += 1;
        return (zhban_shape_t *)item;
    }

    item = get_idle_shape(z, strsize);
    memcpy(item->key, string, strsize);
    item->key_size = strsize;

    shape_string(z, item);

    HASH_ADD_KEYPTR(hh, z->shaper_cache, item->key, item->key_size, item);
    DL_APPEND(z->shaper_history, item);
    z->outer.shaper_size += shape_sizeof(item);
    ZHBAN_INCREF(item->refcount);

    return (zhban_shape_t *)item;
}

void zhban_release_shape(zhban_t *zhban, zhban_shape_t *zs) {
    shape_t *s = (shape_t *)zs;

    if (ZHBAN_GETREF(s->refcount) == 0)
        log_fatal((zhban_internal_t *)zhban, "releasing already free shape");
    ZHBAN_DECREF(s->refcount);
}

//}
//{ bitmap_t
typedef struct _bitmap {
    zhban_bitmap_t bitmap;

    shape_t *shape;       /* out there in the shaper. also - key. */

    uint32_t data_allocd;

    UT_hash_handle hh;
    struct _bitmap *prev;
    struct _bitmap *next;
} bitmap_t;

static uint32_t bitmap_sizeof(const bitmap_t *p) {
    return sizeof(bitmap_t) + p->data_allocd;
}

static uint32_t bitmap_data_expected_size(const shape_t *zs) {
    /* FIXME for vertical scripts - cluster map strip */
    return (zs->shape.w + 0) * (zs->shape.h + 1) * 4;
}

static uint32_t bitmap_expected_sizeof(const shape_t *shape) {
    return sizeof(bitmap_t) + bitmap_data_expected_size(shape);
}

static bitmap_t *reallocate_bitmap(const shape_t *shape, bitmap_t *bitmap) {
    if (!bitmap) {
        bitmap = malloc(sizeof(bitmap_t));
        memset(bitmap, 0, sizeof(bitmap_t));
    } else {
        if (bitmap->shape) {
            ZHBAN_DECREF(bitmap->shape->refcount);
            bitmap->shape = NULL;
        }
    }
    uint32_t data_size = bitmap_data_expected_size(shape);
    if (bitmap->data_allocd < data_size) {
        bitmap->data_allocd = data_size;
        bitmap->bitmap.data = realloc(bitmap->bitmap.data, bitmap->data_allocd);
    }
    return bitmap;
}

static void drop_bitmap(bitmap_t *z) {
    if (z->shape)
        ZHBAN_DECREF(z->shape->refcount);
    free(z->bitmap.data);
    free(z);
}

static void drop_bitmap_cache(bitmap_t **head) {
    bitmap_t *elt, *tmp;
    HASH_ITER(hh, *head, elt, tmp) {
        HASH_DEL(*head, elt);
        drop_bitmap(elt);
    }
}

/* return a shape_t that can be (re)used for a given key_size minding cache size limit. */
static bitmap_t *get_idle_bitmap(zhban_internal_t *z, const shape_t *shape) {
    bitmap_t *item, *evicted_item = NULL, *tmp;
    uint32_t needed_space = bitmap_expected_sizeof(shape);

    /* if we are over the cache size limit, clean up some. */
    DL_FOREACH_SAFE(z->bitmap_history, item, tmp) {
        /* if we have enough space at last .. */
        if (needed_space < (z->outer.bitmap_limit - z->outer.bitmap_size)) {
            if (evicted_item)
                /* got it by eviction, reuse item so as to not do free/alloc dance */
                item = evicted_item;
            else
                /* just have free space here; reallocate_shape() will allocate new item */
                item = NULL;
            break;
        }

        /* drop evicted item if we need to evict more that one */
        if(evicted_item)
            drop_bitmap(evicted_item);

        HASH_DELETE(hh, z->bitmap_cache, item);
        DL_DELETE(z->bitmap_history, item);
        z->outer.bitmap_size -= bitmap_sizeof(item);
        z->outer.bitmap_evictions += 1;
        evicted_item = item;
    }

    bitmap_t *rv = reallocate_bitmap(shape, item);

    /* grow cache to avoid thrashing (?) */
    if (z->outer.bitmap_limit < z->outer.bitmap_size) {
        log_trace(z, "grew cache from %d to %d", z->outer.bitmap_limit, z->outer.bitmap_size);
        z->outer.bitmap_limit = z->outer.bitmap_size;
    }

    return rv;
}

//}
//{ renderer
static void render_shape(zhban_internal_t *z, bitmap_t *item) {
    shape_t *sh = item->shape;

    memset(item->bitmap.data, 0, (sh->shape.w) * (sh->shape.h + 1) * 4);

    const  int32_t  pitch = sh->shape.w;  /* must be signed, or hilarity ensues */
    const uint32_t *first_pixel = item->bitmap.data;
    const uint32_t *last_pixel = item->bitmap.data + sh->shape.w * sh->shape.h - 1;

    item->bitmap.cluster_map = item->bitmap.data + sh->shape.w * sh->shape.h;
    item->bitmap.data_size = sh->shape.w * sh->shape.h * 4;
    item->bitmap.cluster_map_size = sh->shape.w * 4;

    int x_cmlimit;
    const uint32_t glyph_count =  sh->glyphs_used/sizeof(glyph_info_t);

    log_trace(z, "renderering into %dx%d origin %d,%d shape %p",
                sh->shape.w, sh->shape.h, sh->shape.origin_x, sh->shape.origin_y, sh);

    for (uint32_t glyph_i = 0; glyph_i < glyph_count; glyph_i++) {
        glyph_info_t *g_info = sh->glyphs + glyph_i;
        glyph_t *glyph = g_info->glyph;

        const uint32_t span_count = glyph->spans_used/sizeof(span_t);

        /* FIXME: pixel format, endianness */
        const uint32_t attribute_shifted = (g_info->cluster & 0xFFFFu)<<16;
        const uint32_t attribute_mask = 0x0000FFFFU;

        int32_t gx = g_info->x_origin >> 6; /* translate */
        int32_t gy = g_info->y_origin >> 6; /* to pixels */
        uint32_t *origin = item->bitmap.data + pitch * gy + gx;

        log_trace(z, "rendering glyph %d at  %d,%d", glyph_i, gx, gy);

        for (uint32_t span_i = 0; span_i < span_count; span_i++) {
            span_t   *span = glyph->spans + span_i;
            uint32_t *start = origin + span->y * pitch + span->x;

            if (start >= first_pixel) {
                if (start + span->len <= last_pixel) {
                    for (int x = 0; x < span->len; x++) {
                        /* FIXME: pixel format, endianness */
                        uint32_t t = *start & attribute_mask;
                        t |= attribute_shifted | span->coverage;
                        *start++ = t;
                    }
                } else {
                    log_error(z, "  error: overflow (origin=%p start=%p fp=%p lp=%p )", origin, start, first_pixel, last_pixel);
                    log_info(z,  "  span %d x [%d,%d] y %d | abs [%d,%d] y %d", span_i, span->x, span->x + span->len, span->y,
                            gx + span->x, gx + span->x + span->len, gy + span->y);
                }
            } else {
                log_error(z, "  error: underflow (origin=%p start=%p fp=%p lp=%p )", origin, start, first_pixel, last_pixel);
                log_info(z,  "  span %d x [%d,%d] y %d | abs [%d,%d] y %d", span_i, span->x, span->x + span->len, span->y,
                        gx + span->x, gx + span->x + span->len, gy + span->y);
            }
        }

        /* naive cluster map: just set from x_origin to next_x_origin (disregards offsets, etc) */
        /* FIXME: vertical scripts */ /* FIXME!! */ /* FIIIIXXXXXMMEEEE */
        x_cmlimit = (glyph_i == sh->glyphs_used - 1) ? (sh->glyphs[glyph_i+1].x_origin)>>6 : sh->shape.w;
        x_cmlimit = x_cmlimit > sh->shape.w ? sh->shape.w : x_cmlimit;
        x_cmlimit = x_cmlimit < 0 ? 0 : x_cmlimit;

        log_trace(z, "clustermapping glyph %d cluster %d x,y %d,%d x_cmlimit %d", glyph_i,
                    g_info->cluster, g_info->x_origin>>6, g_info->y_origin>>6, x_cmlimit);
        for (int32_t cj = g_info->x_origin>>6; cj < x_cmlimit; cj++)
            item->bitmap.cluster_map[cj] = g_info->cluster;
    }
}

zhban_bitmap_t *zhban_render_pp(zhban_t *zhban, zhban_shape_t *zshape, zhban_postproc_t pp, void *u) {
    zhban_internal_t *z = (zhban_internal_t *)zhban;
    shape_t *shape = (shape_t *)zshape;
    bitmap_t *item;
    z->outer.bitmap_gets += 1;

    HASH_FIND(hh, z->bitmap_cache, &shape, sizeof(zhban_t *), item);
    if (item) {
        /* put the item at the head of history list */
        DL_DELETE(z->bitmap_history, item);
        DL_APPEND(z->bitmap_history, item);
        z->outer.bitmap_hits += 1;
        return (zhban_bitmap_t *)item;
    }

    item = get_idle_bitmap(z, shape);
    item->shape = shape;
    ZHBAN_INCREF(item->shape->refcount);

    render_shape(z, item);

    HASH_ADD_KEYPTR(hh, z->bitmap_cache, &(item->shape), sizeof(zhban_t *), item);
    DL_APPEND(z->bitmap_history, item);
    z->outer.bitmap_size += bitmap_sizeof(item);

    if (pp)
        pp((zhban_bitmap_t *)item, zshape, u);

    return (zhban_bitmap_t *)item;
}

zhban_bitmap_t *zhban_render(zhban_t *zhban, zhban_shape_t *zshape) {
    return zhban_render_pp(zhban, zshape, NULL, NULL);
}

void zhban_pp_color(zhban_bitmap_t *b, zhban_shape_t *s ATTR_UNUSED, void *u) {
    /* FIXME: endianness */
    uint32_t color = (*(uint32_t *)u) & 0x00FFFFFFu;
    for(uint32_t i = 0; i < b->data_size/4; i++) {
        uint32_t alpha = (b->data[i] & 0xFFu) << 24;
        if (alpha)
            b->data[i] = color | alpha;
        else
            b->data[i] = 0;
    }
}

void zhban_pp_color_vflip(zhban_bitmap_t *b, zhban_shape_t *s ATTR_UNUSED, void *u) {
    /* FIXME: endianness */
    uint32_t color = (*(uint32_t *)u) & 0x00FFFFFFu;
    bitmap_t *bitmap = (bitmap_t *)b;
    uint32_t *buf = malloc(bitmap->data_allocd);
    for (int32_t y = 0; y < s->h ; y++) {
        for (int32_t x = 0; x < s->w ; x++) {
            uint32_t i =  x + s->w * y;
            uint32_t j =  x + s->w * (s->h - y - 1);
            uint32_t alpha = (b->data[i] & 0xFFu) << 24;
            if (alpha)
                buf[j] = color | alpha;
            else
                buf[j] = 0;
        }
    }
    free(b->data);
    b->data = buf;
}
//}
