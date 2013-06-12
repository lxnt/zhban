Wotsit?
-------

This is a take on twin-threaded text shaping and rendering using `HarfBuzz <http://harfbuzz.org>`__ 
and `FreeType <http://freetype.org>`__.

The goal is to support splitting text layout - line-splitting, column alignment, etc, and
text rendering - converting character sequences and coordinates into pixel data  - between 
two threads in a lockless manner while caching intermediate results.

Main driver behind this effort is the need to replace SDL_ttf -based text renderer in 
the `Dwarf Fortress <http://www.bay12games.com/dwarves/>`__ with something that both better utilizes 
multiple CPU cores and delivers better-looking results.


How it works
------------

``zhban_open()``, supplied with font data and desired line height (width for vertical scripts), in pixels, gives back a handle.

After that the text layout thread can call ``zhban_size()`` on an utf-16 string to determine its bounding box.

When the text layout code has done its work and ended up with a bunch of strings, they are passed in some arbitrary
manner to the rendering thread, which then calls ``zhban_render()`` and gets a bunch of pixels 
in RG16UI format, which can then be uploaded as a texture or used in any other way.

The R channel holds the pixel 'intensity', while the G channel holds the index of the utf-16 character that caused the pixel in question
to have non-zero intensity. Currently the ligatures end up with the index of their first charater.


Resources are released by a call to ``zhban_drop()``


This all is very much a work in progress, especially in cache-tuning and texture upload optimization, so beware.


Files
-----

``zhban.c, zhban.h`` - core code. You can use ``cmake`` to build a shared library.

``cyzhban.pyx, cyzhban.pxd`` - Cython bindings. Untested. You can use ``setup.py`` to build a python extension module.

``zhban.py`` - ctypes Python bindings. You can try to run ``python test.py font.ttf some text`` after you have built and put the shared library somewhere.
Requires recent pygame2 and SDL2.


