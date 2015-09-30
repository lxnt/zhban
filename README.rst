Wotsit?
-------

This is a take on aggressively cached lockless text shaping and rendering using `HarfBuzz <http://harfbuzz.org>`__
and `FreeType <http://freetype.org>`__.

Main driver behind this effort was the need to replace SDL_ttf -based text renderer in
the `Dwarf Fortress <http://www.bay12games.com/dwarves/>`__ with something that both better utilizes
multiple CPU cores and delivers better-looking results.


How to use
----------

``zhban_open()``, supplied with font data, desired line height, in pixels and some other parameters gives back a pointer to structure,
which is both a handle and a statistics container.

Other parameters include glyph, shape and bitmap cache limits, a subpixel positioning flag, and logging stuff.

Subpixel positioning means positioning glyphs with subpixel precision, to 1/64th of a pixel.
This affects how a glyph is rendered by FreeType, and thus grows typical glyph cache size by a factor of 10 to 100 - an entry
for each subpixel offset used per glyph - in exchange for text looking exactly as font designer intended.

``zhban_shape()`` accepts an UTF-16 encoded string, shapes it (determines which glyphs to place where), and returns ``zhban_shape_t``
structure, defining string bounding box and origin offset.

At this point a reference count is incremented for the shape structure, so that it, and any glyphs it consists of, are guaranteed to not be
dropped from respective caches. Note that this means that glyph and shape cache size limits are soft - that is, they can be exceeded,
if reference counts prevent dropping least recently used records.

Origin offset determines where, relative to the  left bottom corner of the bounding box/bitmap, does the first glyph origin lies.

Consider that no matter what line height you request, there almost always are individual glyphs that are either smaller or larger than that.
For example, the word 'can' will have minimal height when rendered, whereas the bitmap for the word 'really' will have significantly larger
height due to 'l' and 'y' glyphs. The only thing in common between these two strings is the baseline - imaginary line, to which all glyphs
are somehow attached. Origin offset is the offset to start of this baseline from the lower-left corner of the bitmap, thus allowing to align
all the bitmaps.

After shaping is done you end up with multiple boxes, each representing a word or a string. At this point, text layout, line splitting, etc
can be done.

``zhban_render()`` accepts a shape pointer received from ``zhban_shape()`` and returns a pointer to a structure containing
a rendered bitmap of the shape. This pointer is valid only up to next call to ``zhban_render()``.

The bitmap is in OpenGL RG16UI format. The R channel contains 'intensity' and the G channel contains cluster attribution -
an index, starting from 0, of the UTF-16 character (technically called 'cluster', since there might be multiple
glyphs representing one Unicode code point, or vice versa) that caused the pixel in question to ahve non-zero intensity. This is indended
to be used in multi-colored text. Currently the ligatures end up with the index of their first charater.

``zhban_bitmap_t::cluster_map`` contains same cluster attribution but for the zero-intensity pixels. It is a strip of same width as the bitmap,
roughly indicating which cluster a pixel column corresponds to. This is intended for coloring background per-character, showing text selection
as color inversion and the like.

``zhban_render_pp()`` accepts a post-processing function which can be used to convert and cache the bitmap from the default RG16UI format.

``zhban_pp_color()`` is a convenience post-processor, converting RG16UI bitmap into a RGBA8UI one, single color.

``zhban_pp_color_vflip()`` does the same, but also flips the bitmap vertically, so it can be directly supplied to, for example,
``SDL_CreateRGBSurfaceFrom()``

After you have done whatever it is you wanted to with the bitmap, you must call ``zhban_release_shape()`` on the shape,
so that the reference count is decremented. Otherwise the shape cache will grow unbounded.

Helper functions include UTF-8 strlen() and UTF-8 to UTF-16 and back converters.

``zhban_close()`` cleans everything up.

See ``test.py`` and ``testwrap.py`` for examples.


Intended mode of operation
--------------------------

``zhban_t`` pointer is intended to be shared among the pair of threads.
Cache statistics there are written without any locking or atomic ops, thus they cannot be expected to be absolutely accurate.

``zhban_shape()`` and ``zhban_render()`` are intended to be called from two different threads. This means that one thread only calls ``zhban_shape()``,
passes ``zhban_shape_t``-s to the other, which only calls ``zhban_render()``. Multiple threads either on the shaping or on the rendering side
are not supported. You can use multiple ``zhban_t``-s, one per a pair of threads if you feel inclined to. Also if multiple font sizes/fonts are desired.

``zhban_shape_t``-s from ``zhban_shape()`` are intended to be passed from shape thread to render thread and back by some means external to this library.

After the render thread is done with a ``zhban_shape_t`` (usually after calling ``zhban_render()`` on it), it should pass it back to the shape thread,
where it is to be duly supplied to ``zhban_release_shape()``.

``zhban_shape()`` increments refcount on ``zhban_shape_t`` it returns, thus guaranteeing that the pointer stays valid
up until ``zhban_release_shape()`` is called from that same thread. Atomic refcounting is contemlated, but not decided on yet.


Files
-----

``zhban.h, zhban-internal.h zhban.c, utf.c logging.c`` - core code. use ``cmake`` to build.

``zhban.py`` - ctypes Python bindings.

``test.py`` - renders multiple paragraphs of text. usage: ``python3 test.py path/to/font.ttf some_text_file``.
Reflows text on resize. Requires `py-sdl2  <https://bitbucket.org/marcusva/py-sdl2>`__ and `SDL2 <http://www.libsdl.org/>`__.

``divide.py`` - line-breaking code taken from http://xxyxyz.org/line-breaking/

``cyzhban.pyx, cyzhban.pxd, setup.py`` - stale Cython bindings.

[Screenshot](para3.png)


