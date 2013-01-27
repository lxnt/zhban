# -*- encoding: utf-8 -*-

import os
import sys
import ctypes

ctypes.pythonapi.PyBytes_FromStringAndSize.restype = ctypes.py_object

__all__ = ["zhban_rect_t","Zhban", "ZhbanFail"]

class zhban_t(ctypes.Structure):
    """ opaque, basically a face with associated caches and stuff """

class zhban_rect_t(ctypes.Structure):
    """ Bounding box bound to first glyph origin optionally with the render result """
    def __repr__(self):
        return "zhban_rect_t(w={} h={} bo={} bs={})".format(self.w, self.h, self.baseline_offset, self.baseline_shift)
    @property
    def data(self):
        return ctypes.pythonapi.PyBytes_FromStringAndSize(self._data, self.w*self.h*4)

zhban_rect_t._fields_ = [
    ("_data", ctypes.c_void_p),
    ("w", ctypes.c_uint),
    ("h", ctypes.c_uint),
    ("baseline_offset", ctypes.c_int),
    ("baseline_shift", ctypes.c_int),
]

def bind(libpath = None):
    name = "zhban"
    libname = dict(win32="{0}.dll", darwin="lib{0}.dylib", cli="{0}.dll").get(sys.platform, "lib{0}.so").format(name)
    if libpath:
        lib = ctypes.CDLL(os.path.join(libpath, libname))
    elif 'PGLIBDIR' in os.environ:
        lib = ctypes.CDLL(os.path.join(os.environ['PGLIBDIR'], libname))
    else:
        lib = ctypes.CDLL(ctypes.util.find_library(name))
    lib.zhban_open.restype = ctypes.POINTER(zhban_t)
    lib.zhban_open.argtypes = [ ctypes.c_void_p,        # data
                                ctypes.c_uint,          # size
                                ctypes.c_int,           # pixheight
                                ctypes.c_uint,          # sizerlimit
                                ctypes.c_uint,          # renderlimit
                                ]
    lib.zhban_drop.restype = None
    lib.zhban_drop.argtypes = [ ctypes.POINTER(zhban_t) ]

    lib.zhban_size.restype = ctypes.c_int
    lib.zhban_size.argtypes = [
        ctypes.POINTER(zhban_t),        # zhban
        ctypes.c_void_p,                # string
        ctypes.c_uint,                  # strsize
        ctypes.POINTER(zhban_rect_t)    # rv
    ]

    lib.zhban_render.restype = ctypes.c_int
    lib.zhban_render.argtypes = [
        ctypes.POINTER(zhban_t),        # zhban
        ctypes.c_void_p,                # string
        ctypes.c_uint,                  # strsize
        ctypes.POINTER(zhban_rect_t)    # rv
    ]

    return lib

class ZhbanFail(Exception):
    pass

class Zhban(object):
    def __init__(self, fontbuf, pixheight, szlim = 100500, rrlim = 100500, libpath = None):
        self._s_rect = zhban_rect_t()
        self._r_rect = zhban_rect_t()
        if type(fontbuf) is bytes:
            self._fbuf = fontbuf
        else:
            self._fbuf = bytes(fontbuf) # have to copy it.
        self._lib = bind(libpath)
        self._z = self._lib.zhban_open(self._fbuf, len(self._fbuf), pixheight, szlim, rrlim)
        if not bool(self._z):
            raise ZhbanFail

    @staticmethod
    def _bufconv(ass):
        if type(ass) is str:
            buf = ass.encode("utf-16")
        elif type(ass) is bytes:
            buf = ass
        else:
            raise TypeError
        return buf

    def size(self, ass):
        buf = self._bufconv(ass)
        rv = self._lib.zhban_size(self._z, buf, len(buf), ctypes.byref(self._s_rect))
        if rv != 0:
            raise ZhbanFail
        return self._s_rect

    def render(self, ass, rect):
        buf = self._bufconv(ass)
        self._r_rect.w = rect.w
        self._r_rect.h = rect.h
        self._r_rect.baseline_offset = rect.baseline_offset
        self._r_rect.baseline_shift = rect.baseline_shift
        rv = self._lib.zhban_render(self._z, buf, len(buf), ctypes.byref(self._r_rect))
        if rv != 0:
            raise ZhbanFail
        return self._r_rect
