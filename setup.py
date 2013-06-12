from distutils.core import setup
from distutils.extension import Extension
from Cython.Distutils import build_ext
import subprocess

def pkgconfig(*packages, **kw):
    flag_map = {'-I': 'include_dirs', '-L': 'library_dirs', '-l': 'libraries'}
    output = subprocess.check_output(['pkg-config','--libs', '--cflags' ] + list(packages), universal_newlines=True)
    for token in output.split():
        kw.setdefault(flag_map.get(token[:2]), []).append(token[2:])
    return kw

setup(
    cmdclass = {'build_ext': build_ext},
    ext_modules = [
        Extension("cyzhban", ["cyzhban.pyx", "zhban.c"],
            extra_compile_args = [ '-std=c99' ],
            **pkgconfig('harfbuzz', 'freetype2')
        )
    ]
)
