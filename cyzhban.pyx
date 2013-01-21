cimport cyzhban
from cpython.buffer cimport PyObject_GetBuffer, PyBuffer_Release, PyBUF_CONTIG_RO

cdef class simprinter:
    cdef cyzhban.zhban_t *_zhban
    cdef cyzhban.zhban_rect_t *sizer_rv
    cdef cyzhban.zhban_rect_t *render_rv
    
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
        pass

    cdef texture(self, text, size):
        pass
