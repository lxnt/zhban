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

/* Thread safety:

    Intended operation is for the zhban_t pointer to be shared between two threads,
    one repeatedly calling zhban_size(), doing layout, then somehow passing
    resulting string and final rectangle to the other, which repeatedly calls zhban_render().

    Caches and all FreeType data is kept in two sets, each used only by one of the functions.
*/

typedef struct _zhban zhban_t;

typedef struct _zhban_rect {
    uint32_t *data; // RG_16UI of (intensity, index_in_source_string), or NULL. w*h*4 bytes.
    uint32_t *cluster_map; // w cluster indices for background. go from glyph origin to next glyph origin.
    int32_t w, h;
    int32_t origin_x, origin_y;
} zhban_rect_t;

#define ZHLOG_TRACE 5
#define ZHLOG_INFO  4
#define ZHOGL_WARN  3
#define ZHLOG_ERROR 2
#define ZHOGL_FATAL 1
typedef void (*logsink_t)(const int level, const char *fmt, va_list ap);

/* prepare to use a font face that FreeType2 can handle.
    data, size - buffer with the font data (must not be freed or modified before drop() call)
    pixheight - desired line interval in pixels
    tabstep - tabs skip to next multiple of this in pixels. value <= 0 uses emwidth of the font.
    sizerlimit, renderlimit - cache limits in bytes, excluding uthash overhead.
    verbose - primitive log toggle. spams stdout when nonzero.
*/
ZHB_EXPORT zhban_t *zhban_open(const void *data, const uint32_t size,
                                            int pixheight, int tabstep,
                                            uint32_t sizerlimit, uint32_t renderlimit,
                                            int llevel, logsink_t lsink);
ZHB_EXPORT void zhban_drop(zhban_t *);

/* returns expected size of bitmap for the string in rv. data pointer is NULL.
   params:
    in
        face - which face to size for
        string - UCS-2 string buffer
        strsize - string buffer size in bytes
    out
        rv - rect w/o render
    return value - nonzero on error
*/
ZHB_EXPORT int zhban_size(zhban_t *zhban, const uint16_t *string, const uint32_t strsize,
                                                                            zhban_rect_t *rv);

/* returns cached bitmap of the string in rv, read-only, subsequent calls invalidate data pointer.
   params:
    in
        face - which face to shape and render with
        string - UCS-2 string buffer
        strsize - string buffer size in bytes
        w - renderer string width in pixels, previously returned by zhban_size() for this zhban
            and string
        rv - result of previous sizing. rv->data is irrelevant, rv->{w,h,bs,bo} must be valid.
    out
        rv - rendered bitmap in rv->data.
            rendered bitmap is GL_RG16UI format, R component is intensity, G component is index of
            UCS-2 codepoint in the string that caused the pixel to be rendered.
            Value is 0 for zero intensity pixels.
            Extra row (h -th) contains cluster(codepoint) index map

    return value: nonzero on error.
*/
ZHB_EXPORT int zhban_render(zhban_t *zhban, const uint16_t *string, const uint32_t strsize,
                                                                            zhban_rect_t *rv);

#if defined(__cplusplus)
#define extern_C_curly_closes }
extern_C_curly_closes
#endif
#endif
