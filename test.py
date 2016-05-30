#!/usr/bin/python3
# -*- encoding: utf-8 -*-

import os, sys, ctypes, time, struct
import re, argparse, subprocess, textwrap
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

def init(size=(480, 320), title='zhban test', icon=None, resizable = True):
    if resizable:
        flags = sdl2.video.SDL_WINDOW_OPENGL | sdl2.video.SDL_WINDOW_RESIZABLE
    else:
        flags = sdl2.video.SDL_WINDOW_OPENGL
    window = sdl2.SDL_CreateWindow(title.encode('utf-8'),
        sdl2.video.SDL_WINDOWPOS_UNDEFINED, sdl2.video.SDL_WINDOWPOS_UNDEFINED,
        size[0], size[1], flags)
    if icon:
        sdl2.video.SDL_SetWindowIcon(window, icon)
    renderer = sdl2.render.SDL_CreateRenderer(window, -1, sdl2.render.SDL_RENDERER_ACCELERATED)

    return (window, renderer)

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
    sdl2.render.SDL_RenderPresent(renderer)
    sdl2.render.SDL_DestroyTexture(tex)

def getRGBAsurf(w, h, data = None):
    bpp   = ctypes.c_int()
    rmask = ctypes.c_uint()
    gmask = ctypes.c_uint()
    bmask = ctypes.c_uint()
    amask = ctypes.c_uint()
    sdl2.pixels.SDL_PixelFormatEnumToMasks(sdl2.pixels.SDL_PIXELFORMAT_ABGR8888,
        ctypes.byref(bpp), ctypes.byref(rmask), ctypes.byref(gmask), ctypes.byref(bmask), ctypes.byref(amask))
    if data is not None:
        return sdl2.surface.SDL_CreateRGBSurfaceFrom(data, w, h, bpp, w*4, rmask, gmask, bmask, amask)
    else:
        return sdl2.surface.SDL_CreateRGBSurface(0, w, h, bpp, rmask, gmask, bmask, amask)

class StatsWindow(object):
    def __init__(self, font, size, otherz):
        self.z = Zhban(open(font, "rb").read(), size,
            loglevel=4, subpix=True, libpath = os.environ.get('PYSDL2_DLL_PATH'))
        self.otherz = otherz

        s = self.z.shape(self.z.ppstats[0])
        self.w = s.contents.w + 32
        self.h = (len(self.z.ppstats) + 1) * self.otherz.line_step + 32

        self.z.release_shape(s)

        self.win, self.ren = init((self.w, self.h), title = "zhban cache stats", resizable = False)

    def update(self):
        color = (0xB0, 0xF0, 0xB0)
        x, y = 16, self.z.line_step
        surf = getRGBAsurf(self.w, self.h)

        for l in self.otherz.ppstats:

            shape = self.z.shape(l)
            bitmap = self.z.render_colored(shape, color, vflip=True)

            bsurf = getRGBAsurf(shape.contents.w, shape.contents.h, bitmap.contents._data)

            sdl2.surface.SDL_SetSurfaceBlendMode(bsurf, sdl2.blendmode.SDL_BLENDMODE_NONE)
            srcrect = sdl2.rect.SDL_Rect(0, 0, shape.contents.w, shape.contents.h)

            dst_x = x + shape.contents.origin_x
            dst_y = y - (shape.contents.h - shape.contents.origin_y)

            dstrect = sdl2.rect.SDL_Rect(dst_x, dst_y, shape.contents.w, shape.contents.h)
            sdl2.surface.SDL_BlitSurface(bsurf, ctypes.byref(srcrect), surf, ctypes.byref(dstrect))

            sdl2.surface.SDL_FreeSurface(bsurf)
            self.z.release_shape(shape)
            y += self.z.line_step

        vp = sdl2.rect.SDL_Rect(0, 0, self.w, self.h)
        sdl2.render.SDL_RenderSetViewport(self.ren, ctypes.byref(vp))
        drawsurf(self.ren, surf, True, True)
        sdl2.surface.SDL_FreeSurface(surf)

    def finalize(self):
        self.z.fini()
        sdl2.render.SDL_DestroyRenderer(self.ren)
        sdl2.video.SDL_DestroyWindow(self.win)

def wraptext(zhban, text, w, h, fl_indent = 0, method = divide, dir='ltr', align='left'):
    color = (0xB0, 0xB0, 0xB0)
    surf = getRGBAsurf(w, h)
    line_step = zhban.line_step
    space_advance = zhban.space_advance

    if dir == 'ltr':
        x = 0
    elif dir == 'rtl':
        x = w
    else:
        raise ZhbalFail("unsupported dir '{}'".format(dir))
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
                        if dir == 'ltr':
                            x += len(shape)
                        else:
                            x -= len(shape)
                        continue
                    bitmap = zhban.render_colored(shape, color, vflip = True)
                    bsurf = getRGBAsurf(shape.w, shape.h, bitmap.contents._data)
                    sdl2.surface.SDL_SetSurfaceBlendMode(bsurf, sdl2.blendmode.SDL_BLENDMODE_NONE)
                    srcrect = sdl2.rect.SDL_Rect(0, 0, shape.w, shape.h)

                    dst_x = x - shape.origin_x
                    dst_y = y - (shape.h - shape.origin_y)

                    dstrect = sdl2.rect.SDL_Rect(dst_x, dst_y, shape.w, shape.h)
                    sdl2.surface.SDL_BlitSurface(bsurf, ctypes.byref(srcrect), surf, ctypes.byref(dstrect))
                    sdl2.surface.SDL_FreeSurface(bsurf)

                    zhban.release_shape(shape)

                    if dir == 'ltr':
                        x += shape.w + space_advance
                    else:
                        x -= shape.w + space_advance

                if dir == 'ltr':
                    x = 0
                else:
                    x = w
                y += line_step
                if y > (h - line_step):
                    break

        y += line_step
        if y > (h - line_step):
            break

    return surf

def main():
    ap = argparse.ArgumentParser(formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=textwrap.dedent("""
    controls:
        b - toggle texture blend mode
        m - toggle linebreaking algorithm
        l - cycle paragraph lead amount
        space - toggle border showing text extents
        q, Esc - quit"""))
    try:
        defont = subprocess.check_output(["fc-match"," serif",  "-f%{file}"])
        open(defont)
    except:
        defont = None
    try:
        mofont = subprocess.check_output(["fc-match"," mono",  "-f%{file}"])
        open(mofont)
    except:
        mofont = defont

    ap.add_argument('-font', metavar='font', type=str, default=defont, help="font file")
    ap.add_argument('-size', type=int, default=18, help='font size')
    ap.add_argument('-f', type=str, metavar='text.file', help='text file (utf-8)')
    ap.add_argument('-dir', type=str, default='ltr', help='direction (ltr, rtl, ttb, btt)')
    ap.add_argument('-script', type=str, help='script: see Harfbuzz src/hb-common.h: Latn, Cyrl, etc')
    ap.add_argument('-lang', type=str, help='language: "en" - english, "ar"- arabic, "ch" - chinese. looks like some ISO code')
    ap.add_argument('-l', type=int, metavar='loglevel', default=4, help='log level (0-5)')
    ap.add_argument('-sp', action="store_true", help='enable subpixel positioning')
    ap.add_argument('words', nargs='*', default=[])
    pa = ap.parse_args()

    if pa.f is None:
        if len(pa.words) == 0:
            ap.error("text file or words as arguments are required")
        text = ' '.join(pa.words)
    else:
        text = open(pa.f, 'r', encoding='utf-8').read()

    if not pa.font:
        ap.error("Font is required.")

    zhban = Zhban(open(pa.font, "rb").read(), pa.size, loglevel=pa.l, subpix=pa.sp,
                            libpath = os.environ.get('PYSDL2_DLL_PATH'))

    zhban.set_script(pa.dir, pa.script, pa.lang)
    if pa.dir == 'rtl':
        align = 'right'
    else:
        align = 'left'

    sdl2.SDL_Init(sdl2.SDL_INIT_VIDEO | sdl2.SDL_INIT_NOPARACHUTE)

    w = 480
    h = 640
    win, lose = init((w,h))

    border = False
    blend = True
    fl_indent = 0
    method = divide
    reflow = False
    surf = wraptext(zhban, text, w - 32, h - 32, fl_indent, method, pa.dir, align)
    ev = sdl2.events.SDL_Event()
    vp = sdl2.rect.SDL_Rect(0, 0, w, h)
    sdl2.render.SDL_RenderSetViewport(lose, ctypes.byref(vp))
    statwin = StatsWindow(mofont, 16, zhban)
    done = False
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
                elif kcode in ( SDLK_q, SDLK_ESCAPE):
                    done = True
                    break
            elif ev.type == sdl2.events.SDL_QUIT:
                done = True
                break
            elif ev.type == sdl2.events.SDL_WINDOWEVENT:
                if ev.window.windowID == sdl2.video.SDL_GetWindowID(win):
                    if ev.window.event == sdl2.video.SDL_WINDOWEVENT_RESIZED:
                        w = ev.window.data1 if ev.window.data1 > 64 else 64
                        h = ev.window.data2 if ev.window.data2 > 64 else 64
                        vp = sdl2.rect.SDL_Rect(0, 0, w, h)
                        sdl2.render.SDL_RenderSetViewport(lose, ctypes.byref(vp))
                        reflow = True
                    elif ev.window.event == sdl2.video.SDL_WINDOWEVENT_CLOSE:
                        done = True
                        break
        if done:
            break

        if reflow:
            sdl2.surface.SDL_FreeSurface(surf)
            surf = wraptext(zhban, text, w - 32, h - 32, fl_indent, method, pa.dir, align)
            reflow = False

        drawsurf(lose, surf, border, blend)
        statwin.update()
        time.sleep(1/8)

    statwin.finalize()
    zhban.fini()
    sdl2.surface.SDL_FreeSurface(surf)
    sdl2.render.SDL_DestroyRenderer(lose)
    sdl2.video.SDL_DestroyWindow(win)
    sdl2.SDL_Quit()


if __name__ == '__main__':
    main()
