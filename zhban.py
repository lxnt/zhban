# -*- encoding: utf-8 -*-

import os
import sys
import ctypes
import codecs

ctypes.pythonapi.PyBytes_FromStringAndSize.restype = ctypes.py_object

__all__ = ["zhban_shape_t", "zhban_bitmap_t", "zhban_postproc_t", "Zhban", "ZhbanFail"]

class zhban_t(ctypes.Structure):
    def __repr__(self):
        return "{:d}/{:d} {:d}/{:d} {:d}/{:d} {:d}/{:d} {:d}/{:d}".format(
            self.shaper_size, self.render_size,
            self.shaper_limit, self.render_limit,
            self.shaper_gets, self.render_gets,
            self.shaper_hits, self.render_hits,
            self.shaper_evictions, self.render_evictions)

zhban_t._fields_ = [
    ("em_width", ctypes.c_uint32),
    ("space_advance", ctypes.c_uint32),
    ("line_step", ctypes.c_uint32),
    ("glyph_size", ctypes.c_uint32),
    ("glyph_limit", ctypes.c_uint32),
    ("glyph_gets", ctypes.c_uint32),
    ("glyph_hits", ctypes.c_uint32),
    ("glyph_evictions", ctypes.c_uint32),
    ("glyph_rendered", ctypes.c_uint32),
    ("glyph_spans_seen", ctypes.c_uint32),
    ("shape_size", ctypes.c_uint32),
    ("shape_limit", ctypes.c_uint32),
    ("shape_gets", ctypes.c_uint32),
    ("shape_hits", ctypes.c_uint32),
    ("shape_evictions", ctypes.c_uint32),
    ("bitmap_size", ctypes.c_uint32),
    ("bitmap_limit", ctypes.c_uint32),
    ("bitmap_gets", ctypes.c_uint32),
    ("bitmap_hits", ctypes.c_uint32),
    ("bitmap_evictions", ctypes.c_uint32)
]

class zhban_shape_t(ctypes.Structure):
    """ Bounding box bound to the first glyph origin, optionally with the render result """
    #0x{:016x} cluster_map=0x{:016x}
    def __repr__(self):
        return "zhban_shape_t(at {!r} w={} h={} ox={} oy={})".format(
            ctypes.byref(self),
            self.w, self.h, self.origin_x, self.origin_y)

    def __len__(self):
        return self.w

zhban_shape_t._fields_ = [
    ("w", ctypes.c_int),
    ("h", ctypes.c_int),
    ("origin_x", ctypes.c_int),
    ("origin_y", ctypes.c_int),
]

class zhban_bitmap_t(ctypes.Structure):
    """ Bounding box bound to the first glyph origin, optionally with the render result """
    def __repr__(self):
        return "zhban_bitmap_t(at {!r} data=0x{:08x}/{:x} cm=0x{:08x}/{:x})".format(
            ctypes.byref(self),
            self._data, self.data_size, self._cluster_map, self.cluster_map_size)

    @property
    def data(self):
        return ctypes.pythonapi.PyBytes_FromStringAndSize(self._data, self.data_size)

    @property
    def cluster_map(self):
        return ctypes.pythonapi.PyBytes_FromStringAndSize(self._cluster_map, self.cluster_map_size)

    def copy(self):
        """ return a shallow copy; data pointer is set to NULL """
        rv = zhban_rect_t.from_buffer_copy(self)
        rv._data = None
        rv._cluster_map = None
        return rv

zhban_bitmap_t._fields_ = [
    ("_data", ctypes.c_void_p),
    ("data_size", ctypes.c_int),
    ("_cluster_map", ctypes.c_void_p),
    ("cluster_map_size", ctypes.c_int),
]

zhban_postproc_t = ctypes.CFUNCTYPE(None,
    ctypes.POINTER(zhban_bitmap_t), ctypes.POINTER(zhban_shape_t), ctypes.c_void_p)

def bind(libpath = None):
    name = "zhban"
    soname = dict(win32="{0}.dll", darwin="lib{0}.dylib", cli="{0}.dll").get(sys.platform, "lib{0}.so").format(name)
    libnames = []
    if libpath:
        libnames.append(os.path.join(libpath, soname))
    cufl = ctypes.util.find_library(name)
    if cufl:
        libnames.append(cufl)
    lib = None
    for libname in libnames:
        lib = ctypes.CDLL(libname)
        if lib:
            break
    if not lib:
        raise ZhbanFail("Can't open library (soname={} libpath={})".format(soname, libpath))

    lib.zhban_open.restype = ctypes.POINTER(zhban_t)
    lib.zhban_open.argtypes = [ ctypes.c_void_p,        # data
                                ctypes.c_uint,          # size
                                ctypes.c_uint,          # pixheight
                                ctypes.c_uint,          # subpixel
                                ctypes.c_uint,          # glyphlimit
                                ctypes.c_uint,          # shaperlimit
                                ctypes.c_uint,          # renderlimit
                                ctypes.c_int,           # log level
                                ctypes.c_void_p         # log sink
                                ]
    lib.zhban_drop.restype = None
    lib.zhban_drop.argtypes = [ ctypes.POINTER(zhban_t) ]

    lib.zhban_shape.restype = ctypes.POINTER(zhban_shape_t)
    lib.zhban_shape.argtypes = [
        ctypes.POINTER(zhban_t),        # zhban
        ctypes.c_void_p,                # string
        ctypes.c_uint,                  # strsize
    ]

    lib.zhban_release_shape.restype = ctypes.POINTER(zhban_bitmap_t)
    lib.zhban_release_shape.argtypes = [
        ctypes.POINTER(zhban_t),        # zhban
        ctypes.POINTER(zhban_shape_t),  # shape
    ]

    lib.zhban_render.restype = ctypes.POINTER(zhban_bitmap_t)
    lib.zhban_render.argtypes = [
        ctypes.POINTER(zhban_t),        # zhban
        ctypes.POINTER(zhban_shape_t),  # shape
    ]

    lib.zhban_render_pp.restype = ctypes.POINTER(zhban_bitmap_t)
    lib.zhban_render_pp.argtypes = [
        ctypes.POINTER(zhban_t),        # zhban
        ctypes.POINTER(zhban_shape_t),  # shape
        ctypes.c_void_p, # callback
        ctypes.c_void_p                 # user data
    ]

    lib.zhban_pp_color.restype = None
    lib.zhban_pp_color.argtypes = [
        ctypes.POINTER(zhban_shape_t),   # shape
        ctypes.POINTER(zhban_bitmap_t),  # bitmap
        ctypes.c_void_p                  # user data
    ]

    lib.zhban_pp_color_vflip.restype = None
    lib.zhban_pp_color_vflip.argtypes = [
        ctypes.POINTER(zhban_shape_t),   # shape
        ctypes.POINTER(zhban_bitmap_t),  # bitmap
        ctypes.c_void_p                  # user data
    ]

    return lib

class ZhbanFail(Exception):
    pass

class Zhban(object):
    def __init__(self, fontbuf, pixheight, subpix = True, gllim = 1<<20, szlim = 1<<16, rrlim = 1<<24, loglevel = 0, libpath = None):
        if type(fontbuf) is bytes:
            self._fbuf = fontbuf
        else:
            self._fbuf = bytes(fontbuf) # have to copy it.
        self._lib = bind(libpath)
        sp = 1 if subpix else 0
        self._z = self._lib.zhban_open(self._fbuf, len(self._fbuf), pixheight, sp, gllim, szlim, rrlim, loglevel, 0)
        if not bool(self._z):
            raise ZhbanFail

        for f in zhban_t._fields_:
            setattr(self, f[0], getattr(self._z.contents, f[0]))

    @staticmethod
    def _bufconv(ass):
        if type(ass) is str:
            buf = ass.encode("utf-16").lstrip(codecs.BOM_LE).lstrip(codecs.BOM_BE)
        elif type(ass) is bytes:
            buf = ass
        else:
            raise TypeError
        return buf

    @property
    def ppstats(self):
        for f in zhban_t._fields_:
            setattr(self, f[0], getattr(self._z.contents, f[0]))
        return (
            "glyph  {: 5d}/{: 5d} {:.03f} {: 8d}/{:d} bytes".format(self.glyph_hits, self.glyph_gets, self.glyph_hits/self.glyph_gets, self.glyph_size, self.glyph_limit),
            "shape  {: 5d}/{: 5d} {:.03f} {: 8d}/{:d} bytes".format(self.shape_hits, self.shape_gets, self.shape_hits/self.shape_gets, self.shape_size, self.shape_limit),
            "bitmap {: 5d}/{: 5d} {:.03f} {: 8d}/{:d} bytes".format(self.bitmap_hits, self.bitmap_gets, self.bitmap_hits/self.bitmap_gets, self.bitmap_size, self.bitmap_limit),
        )

    def shape(self, ass):
        buf = self._bufconv(ass)
        return self._lib.zhban_shape(self._z, buf, len(buf))

    def render_colored(self, shape, color, vflip = False):
        r,g,b = color
        cint = ctypes.c_int((r)|(g<<8)|(b<<16))
        if vflip:
            fp = ctypes.cast(self._lib.zhban_pp_color_vflip, ctypes.c_void_p)
        else:
            fp = ctypes.cast(self._lib.zhban_pp_color, ctypes.c_void_p)
        return self._lib.zhban_render_pp(self._z, shape, fp, ctypes.byref(cint))

    def render(self, shape, postproc = None, pp_data = None):
        if callable(postproc):
            pp = zhban_postproc_t(postproc)
            pp_ptr = ctypes.c_void_p(pp_data)
            return self._lib.zhban_render_pp(self._z, shape, pp, pp_ptr)
        return self._lib.zhban_render(self._z, shape)

    def release_shape(self, shape):
        return self._lib.zhban_release_shape(self._z, shape)
