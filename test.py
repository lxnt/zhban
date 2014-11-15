#!/usr/bin/python3.2

import os, sys, ctypes, time
import pygame2

_pgld = os.environ.get('PGLIBDIR', False)
if _pgld:
    pygame2.set_dll_path(_pgld)
    print("dll path set to " + _pgld)

import pygame2.sdl as sdl
import pygame2.sdl.events as sdlevents
import pygame2.sdl.video as sdlvideo
import pygame2.sdl.surface as sdlsurface
import pygame2.sdl.pixels as sdlpixels
import pygame2.sdl.hints as sdlhints
import pygame2.sdl.render as sdlrender

from pygame2.sdl.rect import SDL_Rect
from pygame2.sdl.video import SDL_Surface
from pygame2.sdl.keycode import *

from zhban import *

def init(size=(480, 320), title='zhban test', icon=None):
    #sdlhints.set_hint(SDL_HINT_RENDER_DRIVER, 'software')
    sdlhints.set_hint(sdlhints.SDL_HINT_FRAMEBUFFER_ACCELERATION, '0') # do not need no window surface
    sdl.init(sdl.SDL_INIT_VIDEO | sdl.SDL_INIT_NOPARACHUTE)
    window = sdlvideo.create_window(title, sdlvideo.SDL_WINDOWPOS_UNDEFINED, sdlvideo.SDL_WINDOWPOS_UNDEFINED, 
        size[0], size[1], sdlvideo.SDL_WINDOW_OPENGL | sdlvideo.SDL_WINDOW_RESIZABLE)
    if icon:
        sdlvideo.set_window_icon(window, icon)
    renderer = sdlrender.create_renderer(window, -1, sdlrender.SDL_RENDERER_ACCELERATED)
    return (window, renderer)

def drawsurf(renderer, surf, b = False):
    sdlrender.set_render_draw_color(renderer, 0, 0, 0, 255)
    sdlrender.render_clear(renderer)
    tex = sdlrender.create_texture_from_surface(renderer, surf)
    sdlrender.set_texture_blend_mode(tex, sdlvideo.SDL_BLENDMODE_NONE)
    vp = sdlrender.render_get_viewport(renderer)
    x = (vp.w - surf._w)//2
    y = (vp.h - surf._h)//2
    if b:
        border = SDL_Rect(x - 1, y - 1, surf._w + 2, surf._h + 2)
        sdlrender.set_render_draw_color(renderer, 255, 0, 0, 255)
        sdlrender.render_draw_rect(renderer, border)
    dst = SDL_Rect(x, y, surf._w, surf._h)
    sdlrender.render_copy_ex(renderer, tex, dstrect=dst, flip=sdlrender.SDL_FLIP_VERTICAL)
    #sdlrender.render_copy(renderer, tex, None, None)

def main():
    win, lose = init()
    z = Zhban(open(sys.argv[1], "rb").read(), 64, loglevel=5)
    text = '\t'.join(sys.argv[2:])
    print(text)
    s = z.size(text)
    print("sized rect {!r}".format(s))
    r = z.render(text, s.copy())
    print("rendered rect {!r}".format(r))
    if True:
        dump = open("dump", "wb")
        dump.write(r.data)
        dump.write(r.cluster_map)
        print("dumped.")

    data = bytes(bytearray(r.data))
    masks = list(sdlpixels.pixelformat_enum_to_masks(sdlpixels.SDL_PIXELFORMAT_ABGR8888))
    #masks = list(sdlpixels.pixelformat_enum_to_masks(sdlpixels.SDL_PIXELFORMAT_RGBA8888))
    bpp = masks.pop(0)
    surf = sdlsurface.create_rgb_surface_from(data, r.w, r.h, bpp, r.w*4, *masks)
    b = True
    while True:
        while True:
            ev = sdlevents.poll_event(True)
            if ev is None:
                break
            elif ev.type == sdlevents.SDL_KEYDOWN:
                kcode = ev.key.keysym.sym
                if kcode == SDLK_SPACE:
                    b = not b
                elif kcode == SDLK_ESCAPE:
                    return
            elif ev.type == sdlevents.SDL_QUIT:
                return

        drawsurf(lose, surf, b)
        sdlrender.render_present(lose)
        time.sleep(0.13)

if __name__ == '__main__':
    main()
