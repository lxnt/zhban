cimport cyzhban
from cpython.buffer cimport Py_buffer, PyObject_GetBuffer, PyBuffer_Release, PyBUF_CONTIG_RO

class SomeError(Exception):
    pass

cdef class simprinter:
    cdef cyzhban.zhban_t *_zhban
    cdef cyzhban.zhban_rect_t sizer_rv
    cdef cyzhban.zhban_rect_t render_rv
    
    def __cinit__(self, facebuf, pixheight): # facebuf is bytes, memoryview or something like that..
        cdef Py_buffer buf
        PyObject_GetBuffer(facebuf, &buf, PyBUF_CONTIG_RO)
        self._zhban = cyzhban.zhban_open(buf.buf, buf.len, pixheight, 100500<<10, 100500<<10)
        PyBuffer_Release(&buf)
        if self._zhban is NULL:
            raise MemoryError()

    def __dealloc__(self):
        if self._zhban is not NULL:
            cyzhban.zhban_drop(self._zhban)

    cdef size(self, text):
        cdef Py_buffer buf
        PyObject_GetBuffer(text, &buf, PyBUF_CONTIG_RO)
        fail = zhb_stringrect(self._zhban, <uint16_t *>buf.buf, buf.len, &self.sizer_rv)
        PyBuffer_Release(&buf)
        if fail:
            raise SomeError

    cdef texture(self, text):
        cdef Py_buffer buf
        PyObject_GetBuffer(text, &buf, PyBUF_CONTIG_RO)
        fail = zhb_stringtex(self._zhban, <uint16_t *>buf.buf, buf.len, &self.render_rv)
        PyBuffer_Release(&buf)

        if fail:
            raise SomeError

