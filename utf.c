/*  Copyright (c) 2008-2009 Bjoern Hoehrmann <bjoern@hoehrmann.de>

    Permission is hereby granted, free of charge, to any person
    obtaining a copy of this software and associated documentation
    files (the "Software"), to deal in the Software without restriction,
    including without limitation the rights to use, copy, modify,
    merge, publish, distribute, sublicense, and/or sell copies
    of the Software, and to permit persons to whom the Software
    is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be
    included in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
    OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
    IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
    FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
    OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
    WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

    http://bjoern.hoehrmann.de/utf-8/decoder/dfa/

*/

#include <stdint.h>

#define UTF8_ACCEPT 0
#define UTF8_REJECT 1

static const uint8_t utf8d[] = {
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 00..1f
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 20..3f
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 40..5f
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 60..7f
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, // 80..9f
  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7, // a0..bf
  8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, // c0..df
  0xa,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x4,0x3,0x3, // e0..ef
  0xb,0x6,0x6,0x6,0x5,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8, // f0..ff
  0x0,0x1,0x2,0x3,0x5,0x8,0x7,0x1,0x1,0x1,0x4,0x6,0x1,0x1,0x1,0x1, // s0..s0
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,0,1,0,1,1,1,1,1,1, // s1..s2
  1,2,1,1,1,1,1,2,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1, // s3..s4
  1,2,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,3,1,3,1,1,1,1,1,1, // s5..s6
  1,3,1,1,1,1,1,3,1,3,1,1,1,1,1,1,1,3,1,1,1,1,1,1,1,1,1,1,1,1,1,1, // s7..s8
};

static inline uint32_t
decode(uint32_t* state, uint32_t* codep, uint32_t byte) {
  uint32_t type = utf8d[byte];

  *codep = (*state != UTF8_ACCEPT) ?
    (byte & 0x3fu) | (*codep << 6) :
    (0xff >> type) & (byte);

  *state = utf8d[256 + *state*16 + type];
  return *state;
}

/* returns count of valid code points, and upper bound at invalid cp count */
uint32_t
zhban_8len(uint8_t *s, uint32_t *errors_ptr) {
    uint32_t codepoint;
    uint32_t state = UTF8_ACCEPT;
    uint32_t count, errors = 0;

    for (count = 0; *s; ++s)
        switch (decode(&state, &codepoint, *s)) {
            case UTF8_ACCEPT:
                count += 1;
                break;
            case UTF8_REJECT:
                errors += 1;
                state = UTF8_ACCEPT;
                break;
            default:
                break;
        }

    if (errors_ptr)
        *errors_ptr = count;

    return count;
}

/* returns words written to the dst buffer. if == dstWords, do what? repeat with larger dst buffer.
   skips invalid codepoints. */
uint32_t
zhban_8to16(uint8_t* src, uint32_t srcBytes, uint16_t* dst, uint32_t dstBytes) {
    uint8_t* src_actual_end = src + srcBytes;
    uint8_t* s = src;
    uint16_t* d = dst;
    uint32_t codepoint;
    uint32_t state = 0;

    while (s < src_actual_end) {

        uint32_t dst_words_free = dstBytes/2 - (d - dst);
        uint8_t* src_current_end = s + dst_words_free - 1;

        if (src_actual_end < src_current_end)
            src_current_end = src_actual_end;

        if (src_current_end <= s)
            return dstBytes;

        while (s < src_current_end) {
            if (decode(&state, &codepoint, *s++))
                continue;

            if (codepoint > 0xffff) {
             /* *d++ = (uint16_t)(0xD7C0 + (codepoint >> 10));
                *d++ = (uint16_t)(0xDC00 + (codepoint & 0x3FF)); */
            } else {
                *d++ = (uint16_t)codepoint;
            }
        }
    }

    if ((dstBytes/2 - (d - dst)) > 0)
        *d++ = 0;

    return (d - dst)/2;
}

/* I neither remember copying this function from somewhere, nor writing it myself. */
uint32_t
zhban_16to8(uint16_t* src, uint32_t srcBytes, uint8_t* dst, uint32_t dstBytes) {
    uint8_t *ptr = dst;
    uint32_t i = 0;
    while ((srcBytes/2 > i) && ((ptr - dst) < (dstBytes - 4))) {
        uint16_t c = src[i++];
        if (c <= 0x7f) {
            *ptr++ = c;
            continue;
        }
        if (c <= 0x7ff) {
            *ptr++ = (c >> 6) | 0xc0;
            *ptr++ = (c & 0x3f) | 0x80;
            continue;
        }
        *ptr++ = (c >> 12) | 0xe0;
        *ptr++ = ((c >> 6) & 0x3f) | 0x80;
        *ptr++ = (c & 0x3f) | 0x80;
    }
    *dst = 0;
    return (ptr - dst);
}

uint16_t *
zhban_utf16chr(uint16_t *hay, const uint16_t *haylimit, const uint16_t needle) {
    uint16_t *ptr = hay;
    while(haylimit - ptr > 0)
        if (*ptr == needle)
            return ptr;
        else
            ptr++;
    return ptr;
}
