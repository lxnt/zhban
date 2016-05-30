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

/*  ЛИС ВЛЕЗ В ЖБАН

    Из сёл Вань род Ши, из тех баб:
    от злой дух-лис был ей ох! да ах!;
    да нет сил прочь гнать.
    Был там: ну дверь не дверь, род ширм*... а при нём - жбан. Всяк раз, чуть звук: свёкр близ! - лис шнырь внутрь!; ждёт, скрыт.
    У баб взгляд остр, план тайн-тёмн, без слов.
    Вот раз он влез... Та вмиг горсть ват пих! в горл жбан,
    жбан - в чан, да и греть: буль-буль!
    Стал жар яр, лис - в крик:
     - Что за жар! Ой не тронь, зло мне от тех игр!
    Та - ни гугу. Вопль стал громк... долг он был, - да смолк.
    Та - кляп прочь, глядь:
    шерсть клок да кровь чуть кап-кап.
    И всё.

    - - -
    *А НА ЧТО там был тот жбан? Ляо Чжай о том... не пись-пись?...
    для дам-с...


    http://polusharie.com/index.php?topic=608.150
*/

#if !defined(ZHBAN_H)
#define ZHBAN_H
#if defined(__cplusplus)
#define extern_C_curly_opens extern "C" {
extern_C_curly_opens
#endif

#include <stdarg.h>
#include <stdint.h>

#if defined(__GNUC__)
# define ZHB_EXPORT __attribute__((visibility ("default")))
#else
# define ZHB_EXPORT
#endif

#if defined(USE_SDL2)
#include "SDL.h"
#endif

/* Thread safety:

    Intended operation is for the zhban_t pointer to be shared between two threads,
    one repeatedly calling zhban_size(), doing layout, then somehow passing
    resulting string and final rectangle to the other, which repeatedly calls zhban_render().

    zhban_t data members are strictly read-only.

    Caches and all FreeType data is kept in two sets, each used only by one of the functions.
*/

typedef struct _zhban {
    /* font facts */
    uint32_t em_width;
    uint32_t space_advance;
    uint32_t line_step;

    /* cache statistics */
    uint32_t glyph_size, glyph_limit, glyph_gets, glyph_hits, glyph_evictions;
    uint32_t glyph_rendered, glyph_spans_seen;
    uint32_t shaper_size, shaper_limit, shaper_gets, shaper_hits, shaper_evictions;
    uint32_t bitmap_size, bitmap_limit, bitmap_gets, bitmap_hits, bitmap_evictions;

} zhban_t;

typedef struct _zhban_shape {
    int32_t w, h;               /* bounding box = bitmap size */
    int32_t origin_x, origin_y; /* first glyph origin coords relative to bottom left corner. GL/FT2 coordinate system */
} zhban_shape_t;

typedef struct _zhban_bitmap {
    uint32_t *data;             /* RG_16UI of (intensity, index_in_source_string), or NULL. w*h*4 bytes. */
    uint32_t data_size;         /* size of the above buffer in bytes */
    uint32_t *cluster_map;      /* a row of cluster indices for background. go from glyph origin to next glyph origin. */
    uint32_t cluster_map_size;  /* size of the above buffer in bytes */
} zhban_bitmap_t;

#define ZHLOG_TRACE 5
#define ZHLOG_INFO  4
#define ZHOGL_WARN  3
#define ZHLOG_ERROR 2
#define ZHLOG_FATAL 1

typedef void (*logsink_t)(const int level, const char *fmt, va_list ap);

/* prepare to use a font face that FreeType2 can handle.
    data, size - buffer with the font data (must not be freed or modified before drop() call)
    pixheight - desired line interval in pixels
    tabstep - tabs skip to next multiple of this in pixels. value <= 0 uses multiples of emwidth of the font.
    subpixel_positioning - potentially better-looking results at the expense of glyph cache size (if nonzero)
    sizerlimit, renderlimit - cache limits in bytes, excluding out-of-structure uthash overhead.
    verbose - primitive log toggle. spams stdout when nonzero.
*/
ZHB_EXPORT zhban_t *zhban_open(const void *data, const uint32_t size,
                                uint32_t pixheight,
                                uint32_t subpixel_positioning,
                                uint32_t glyphlimit, uint32_t sizerlimit, uint32_t renderlimit,
                                int llevel, logsink_t lsink);
ZHB_EXPORT void zhban_drop(zhban_t *);

/* HarfBuzz specifics for non-latin/cyrillic scripts:
    direction:  ltr, rtl, ttb, btt
    script:     see Harfbuzz src/hb-common.h Latn, Cyrl, etc.
    language:   "en" - english, "ar"- arabic, "ch" - chinese. looks like some ISO code
*/
ZHB_EXPORT void zhban_set_script(zhban_t *zhban, const char *direction, const char *script, const char *language);

/* returns expected size of bitmap for the string in rv. data pointer is NULL.
   params:
    in
        zhban - which zhban to size for
        string - UCS-2 string buffer
        strsize - string buffer size in bytes

    return value - zhban_shape_t* or NULL on error
*/
ZHB_EXPORT zhban_shape_t *zhban_shape(zhban_t *zhban, const uint16_t *string, const uint32_t strsize);

/* releases shape structure when it is not further expected to be used in a call to zhban_render() */
ZHB_EXPORT void zhban_release_shape(zhban_t *zhban, zhban_shape_t *shape);

/* returns cached bitmap of the string in rv, read-only, subsequent calls invalidate data pointer.
   params:
    in
        zhban - which zhban to shape and render with
        shape - shaping results from previous call to zhban_shape()

    return value: zhban_bitmap_t or NULL on error.
*/
ZHB_EXPORT zhban_bitmap_t *zhban_render(zhban_t *zhban, zhban_shape_t *shape);

/* postprocessing callback to mutilate the bitmap/cluster_map data just before it is returned. */
typedef void (*zhban_postproc_t)(zhban_bitmap_t *b, zhban_shape_t *s, void *ptr);

/* calls the supplied callback to post-process the bitmap. */
ZHB_EXPORT zhban_bitmap_t *zhban_render_pp(zhban_t *zhban, zhban_shape_t *shape, zhban_postproc_t pproc, void *ptr);

/* postprocessing convertor RG16UI->RGBA8UI, single color. ptr shall point to the desired color (RGBx), uint32_t */
ZHB_EXPORT void zhban_pp_color(zhban_bitmap_t *bitmap, zhban_shape_t *shape, void *ptr);

/* same as above, but also vertiflips - helper for use in SDL and the like */
ZHB_EXPORT void zhban_pp_color_vflip(zhban_bitmap_t *bitmap, zhban_shape_t *shape, void *ptr);

/* returns count of valid code points, and an upper bound at invalid codepoint count in an UTF-8 string */
ZHB_EXPORT size_t zhban_8len(uint8_t *s, uint32_t *errors_ptr);

/* finds next occurence of needle */
ZHB_EXPORT uint16_t *zhban_utf16chr(uint16_t *hay, const uint16_t *haylimit, const uint16_t needle);

/* converters below skip invalid code points. both return bytes written. buffer sizes in bytes */
ZHB_EXPORT uint32_t zhban_8to16(uint8_t *src, uint32_t srcsize, uint16_t *dst, uint32_t dstsize);
ZHB_EXPORT uint32_t zhban_16to8(uint16_t *src, uint32_t srcsize, uint8_t *dst, uint32_t dstsize);

#if defined(USE_SDL2)
/* caches and returns an GL_RGBA8UI pixel format SDL_Surface */
ZHB_EXPORT SDL_Surface *zhban_sdl_render_rgba(zhban_t *zhban, zhban_shape_t *shape, SDL_Color fg);
ZHB_EXPORT SDL_Surface *zhban_sdl_render_argb(zhban_t *zhban, zhban_shape_t *shape, SDL_Color fg);

ZHB_EXPORT SDL_Surface *zhban_sdl_render_fgbg(zhban_t *zhban, zhban_shape_t *shape, SDL_Color fg, SDL_Color bg);

#endif

#if defined(__cplusplus)
#define extern_C_curly_closes }
extern_C_curly_closes
#endif
#endif
