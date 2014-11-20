#!/usr/bin/python3.2

import os, sys, ctypes, time, struct, re
import sdl2
import sdl2.events
import sdl2.video
import sdl2.surface
import sdl2.blendmode
import sdl2.pixels
import sdl2.render

from sdl2.keycode import *

from zhban import *
from divide import *

def init(size=(480, 320), title='zhban test', icon=None):
    sdl2.SDL_Init(sdl2.SDL_INIT_VIDEO | sdl2.SDL_INIT_NOPARACHUTE)
    window = sdl2.SDL_CreateWindow(title.encode('utf-8'), sdl2.video.SDL_WINDOWPOS_UNDEFINED, sdl2.video.SDL_WINDOWPOS_UNDEFINED,
        size[0], size[1], sdl2.video.SDL_WINDOW_OPENGL | sdl2.video.SDL_WINDOW_RESIZABLE)
    if icon:
        sdl2.video.SDL_SetWindowIcon(window, icon)
    renderer = sdl2.render.SDL_CreateRenderer(window, -1, sdl2.render.SDL_RENDERER_ACCELERATED)
    i = sdl2.render.SDL_RendererInfo()
    sdl2.render.SDL_GetRendererInfo(renderer, ctypes.byref(i))
    return (window, renderer)

def wraptext(zhban, text, w, h, fl_indent = 0, method = divide):
    color = (0xB0, 0xB0, 0xB0)
    bpp   = ctypes.c_int()
    rmask = ctypes.c_uint()
    gmask = ctypes.c_uint()
    bmask = ctypes.c_uint()
    amask = ctypes.c_uint()
    sdl2.pixels.SDL_PixelFormatEnumToMasks(sdl2.pixels.SDL_PIXELFORMAT_ABGR8888,
        ctypes.byref(bpp), ctypes.byref(rmask), ctypes.byref(gmask), ctypes.byref(bmask), ctypes.byref(amask))
    surf = sdl2.surface.SDL_CreateRGBSurface(0, w, h, bpp, rmask, gmask, bmask, amask)
    line_step = zhban.line_step
    space_advance = zhban.space_advance

    x = 0
    y = line_step

    class plead(object):
        def __len__(self):
            return fl_indent * zhban.em_width

    for para in re.split(r"\n+", text):
        if para:
            swords = para.split()
            words = [plead()]
            for sword in swords:
                words.append(zhban.shape(sword).contents)

            def wlen(s):
                return len(s) + space_advance

            lines = divide(words, w, wlen)

            for line in lines:
                for shape in line:
                    if type(shape) is plead:
                        x += len(shape)
                        continue
                    bitmap = zhban.render_colored(shape, color, vflip = True)
                    bsurf = sdl2.surface.SDL_CreateRGBSurfaceFrom(bitmap.contents.data, shape.w, shape.h,
                                                                    bpp, shape.w*4, rmask, gmask, bmask, amask)
                    sdl2.surface.SDL_SetSurfaceBlendMode(bsurf, sdl2.blendmode.SDL_BLENDMODE_NONE)
                    srcrect = sdl2.rect.SDL_Rect(0, 0, shape.w, shape.h)

                    dst_x = x - shape.origin_x
                    dst_y = y - (shape.h - shape.origin_y)

                    dstrect = sdl2.rect.SDL_Rect(dst_x, dst_y, shape.w, shape.h)
                    sdl2.surface.SDL_BlitSurface(bsurf, srcrect, surf, dstrect)

                    zhban.release_shape(shape)

                    x += shape.w + space_advance

                x = 0
                y += line_step
                if y > (h - line_step):
                    break
        y += line_step
        if y > (h - line_step):
            break
    print('\n'.join(zhban.ppstats))
    return surf

def drawsurf(renderer, surf, border, blend):
    sdl2.render.SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255)
    sdl2.render.SDL_RenderClear(renderer)

    tex = sdl2.render.SDL_CreateTextureFromSurface(renderer, surf)
    if blend:
        sdl2.render.SDL_SetTextureBlendMode(tex, sdl2.blendmode.SDL_BLENDMODE_BLEND)
    else:
        sdl2.render.SDL_SetTextureBlendMode(tex, sdl2.blendmode.SDL_BLENDMODE_NONE)
    vp = sdl2.rect.SDL_Rect()
    sdl2.render.SDL_RenderGetViewport(renderer, ctypes.byref(vp))
    x = (vp.w - surf.contents.w)//2
    y = (vp.h - surf.contents.h)//2
    if border:
        border = sdl2.rect.SDL_Rect(x - 1, y - 1, surf.contents.w + 2, surf.contents.h + 2)
        sdl2.render.SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255)
        sdl2.render.SDL_RenderDrawRect(renderer, border)
    dst = sdl2.rect.SDL_Rect(x, y, surf.contents.w, surf.contents.h)
    sdl2.render.SDL_RenderCopy(renderer, tex, None, ctypes.byref(dst))

def main():
    zhban = Zhban(open(sys.argv[1], "rb").read(), 18, loglevel=4)
    text = open(sys.argv[2], 'rb').read().decode('utf-8')

    w = 480
    h = 640
    win, lose = init((w,h))

    border = False
    blend = True
    fl_indent = 0
    method = divide
    reflow = False
    surf = wraptext(zhban, text, w - 32, h - 32, fl_indent, method)
    ev = sdl2.events.SDL_Event()
    vp = sdl2.rect.SDL_Rect(0, 0, w, h)
    sdl2.render.SDL_RenderSetViewport(lose, ctypes.byref(vp))
    while True:
        while sdl2.events.SDL_PollEvent(ctypes.byref(ev), 1):

            if ev is None:
                break
            elif ev.type == sdl2.events.SDL_KEYDOWN:
                kcode = ev.key.keysym.sym
                if kcode == SDLK_SPACE:
                    border = not border
                elif kcode == SDLK_b:
                    blend = not blend
                elif kcode == SDLK_l:
                    fl_indent += 1
                    if fl_indent > 3:
                        fl_indent = 0
                    reflow = True
                elif kcode == SDLK_m:
                    method = divide if method == linear else linear
                    reflow = True
                elif kcode == SDLK_q:
                    return
                elif kcode == SDLK_ESCAPE:
                    return
            elif ev.type == sdl2.events.SDL_QUIT:
                return
            elif ev.type == sdl2.events.SDL_WINDOWEVENT:
                if ev.window.event == sdl2.video.SDL_WINDOWEVENT_RESIZED:
                    w = ev.window.data1 if ev.window.data1 > 64 else 64
                    h = ev.window.data2 if ev.window.data2 > 64 else 64
                    vp = sdl2.rect.SDL_Rect(0, 0, w, h)
                    sdl2.render.SDL_RenderSetViewport(lose, ctypes.byref(vp))
                    reflow = True

        if reflow:
            surf = wraptext(zhban, text, w - 32, h - 32, fl_indent, method)
            reflow = False
        drawsurf(lose, surf, border, blend)
        sdl2.render.SDL_RenderPresent(lose)
        time.sleep(1/8)

if __name__ == '__main__':
    main()
