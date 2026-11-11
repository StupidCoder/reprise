#!/usr/bin/env python3
"""Build the macOS download with checksum-pinned, self-contained dependencies."""
import os
from pathlib import Path
import shutil
import sys

from build_android import ROOT, SDL_VERSION, SDL_SHA, FT_VERSION, FT_SHA, source, run


def main():
    if sys.platform != 'darwin':
        raise SystemExit('This build requires macOS.')
    build = ROOT / 'build'
    deps = build / 'ci-deps'
    deps.mkdir(parents=True, exist_ok=True)
    prefix = build / 'ci-install'
    sdl = source(deps, 'SDL3-' + SDL_VERSION, '.tar.gz',
                 'https://github.com/libsdl-org/SDL/releases/download/release-' +
                 SDL_VERSION + '/SDL3-' + SDL_VERSION + '.tar.gz', SDL_SHA)
    ft = source(deps, 'freetype-' + FT_VERSION, '.tar.xz',
                'https://download.savannah.gnu.org/releases/freetype/freetype-' +
                FT_VERSION + '.tar.xz', FT_SHA)
    jobs = str(min(4, os.cpu_count() or 2))
    flags = ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
             '-DCMAKE_INSTALL_PREFIX=' + str(prefix)]
    for name, src, options in [
        ('sdl', sdl, ['-DSDL_SHARED=ON', '-DSDL_STATIC=OFF',
                      '-DSDL_TEST_LIBRARY=OFF', '-DSDL_EXAMPLES=OFF']),
        ('freetype', ft, ['-DBUILD_SHARED_LIBS=OFF', '-DFT_DISABLE_PNG=ON',
                         '-DFT_DISABLE_BZIP2=ON', '-DFT_DISABLE_HARFBUZZ=ON',
                         '-DFT_DISABLE_BROTLI=ON', '-DFT_REQUIRE_ZLIB=ON']),
    ]:
        target = build / ('ci-' + name)
        run(['cmake', '-S', src, '-B', target, *flags, *options])
        run(['cmake', '--build', target, '-j', jobs])
        run(['cmake', '--install', target])
    # package_macos.py expects the SDL license beside its installed lib directory.
    shutil.copy2(sdl / 'LICENSE.txt', prefix / 'LICENSE.txt')
    run(['cmake', '-S', ROOT, '-B', build, '-G', 'Ninja',
         '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=ON',
         '-DCMAKE_PREFIX_PATH=' + str(prefix),
         '-DSDL3_DIR=' + str(prefix / 'lib/cmake/SDL3'),
         '-DFREETYPE_INCLUDE=' + str(prefix / 'include/freetype2'),
         '-DFREETYPE_STATIC=' + str(prefix / 'lib/libfreetype.a')])
    run(['cmake', '--build', build, '--target', 'reprise', 'shell_checks', 'stunt_lesson_checks', 'library_cache_checks', 'interceptor_lesson_checks', 'zip_import_checks', 'host_touch_checks', '-j', jobs])


if __name__ == '__main__':
    main()
