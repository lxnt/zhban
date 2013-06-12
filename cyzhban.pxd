from libc.stdint cimport int32_t, uint32_t, uint16_t

cdef extern from "zhban.h":
    ctypedef struct zhban_t:
        pass
    
    ctypedef struct zhban_rect_t:
        uint16_t *data
        uint32_t w
        uint32_t h
        int32_t baseline_offset
        int32_t baseline_shift

    zhban_t *zhban_open(void *data, uint32_t size, int pixheight, uint32_t sizerlimit, uint32_t renderlimit)
    void zhban_drop(zhban_t *)

    bint zhban_size(zhban_t *z, uint16_t *string, uint32_t strsize, zhban_rect_t *rv)
    bint zhban_render(zhban_t *z, uint16_t *string, uint32_t strsize, zhban_rect_t *rv)

