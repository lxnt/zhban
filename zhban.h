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
*/

#if !defined(ZHBAN_H)
#define ZHBAN_H
#if defined(__cplusplus)
#define extern_C_curly_opens extern "C" {
extern_C_curly_opens
#endif

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
    uint32_t w, h;
    int32_t baseline_offset;
    int32_t baseline_shift;
} zhban_rect_t;

/* prepare to use a font face that FreeType2 can handle.
    data, size - buffer with the font data (must not be freed or modified before drop() call)
    pixheight - desired line interval in pixels
    sizerlimit, renderlimit - cache limits in bytes, excluding uthash overhead.
*/
ZHB_EXPORT zhban_t *zhban_open(const void *data, const uint32_t size, int pixheight,
                                            uint32_t sizerlimit, uint32_t renderlimit);
ZHB_EXPORT void zhban_drop(zhban_t *);

/* returns expected size of bitmap for the string in rv. data pointed is NULL.
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
    out
        rv - rendered bitmap in rv->data, actual sizes in the rest of members.
            rendered bitmap is RG_16UI format, R component is intensity, G component is index of
            UCS-2 codepoint in the string that caused the pixel to be rendered.
            Value is 0 for zero intensity pixels.

    return value: nonzero on error.
*/
ZHB_EXPORT int zhban_render(zhban_t *zhban, const uint16_t *string, const uint32_t strsize,
                                                                            zhban_rect_t *rv);


#if defined(__cplusplus)
#define extern_C_curly_closes }
extern_C_curly_closes
#endif
#endif
