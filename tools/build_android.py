#!/usr/bin/env python3
"""Build a signed ARM64 SDL test APK with the Android SDK tools (no Gradle)."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SDL_VERSION = '3.4.16'
SDL_SHA = '7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68'
FT_VERSION = '2.14.1'
FT_SHA = '32427e8c471ac095853212a37aef816c60b42052d4d9e48230bab3bdf2936ccc'
NDK_VERSION = '29.0.14206865'
TOOLS_VERSION = '36.1.0'
MIN_API, TARGET_API = 26, 36
ABI = 'arm64-v8a'


def run(args, **kwargs):
    subprocess.run([str(a) for a in args], check=True, **kwargs)


def require(path):
    if not path.exists():
        raise RuntimeError('Missing Android build prerequisite: ' + str(path))
    return path


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''): h.update(chunk)
    return h.hexdigest()


def source(deps, name, suffix, url, sha):
    archive = deps / (name + suffix)
    if not archive.exists():
        temporary = archive.with_name(archive.name + '.download')
        run(['curl', '--fail', '--location', '--retry', '3', '--output', temporary, url])
        if digest(temporary) != sha:
            temporary.unlink()
            raise RuntimeError('Source checksum mismatch: ' + name)
        temporary.replace(archive)
    if digest(archive) != sha:
        raise RuntimeError('Cached source checksum mismatch: ' + str(archive))
    destination = deps / name
    if not (destination / 'CMakeLists.txt').exists():
        with tarfile.open(archive) as tf:
            # Archives are pinned by hash; also reject paths escaping the cache.
            for member in tf.getmembers():
                if not str((deps / member.name).resolve()).startswith(str(deps.resolve()) + os.sep):
                    raise RuntimeError('Invalid archive path')
            tf.extractall(deps)
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', action='store_true', help='Install the built APK using adb')
    parser.add_argument('--serial', help='adb device serial (with --install)')
    parser.add_argument('--version-code', type=int, default=1)
    parser.add_argument('--release', action='store_true', help='Use the release version without the -dev suffix (same signing key)')
    parser.add_argument('--jobs', type=int, default=min(8, os.cpu_count() or 4))
    args = parser.parse_args()
    if args.version_code < 1 or args.jobs < 1: parser.error('version-code and jobs must be positive')
    if args.serial and not args.install: parser.error('--serial requires --install')
    os.environ['PATH'] = '/opt/homebrew/bin:' + os.environ.get('PATH', '')
    sdk = Path(os.environ.get('ANDROID_HOME') or os.environ.get('ANDROID_SDK_ROOT') or
               (Path.home() / ('Library/Android/sdk' if sys.platform == 'darwin' else 'Android/Sdk')))
    ndk = require(Path(os.environ.get('ANDROID_NDK_HOME', sdk / 'ndk' / NDK_VERSION)))
    tools = require(sdk / 'build-tools' / TOOLS_VERSION)
    android_jar = require(sdk / 'platforms' / ('android-' + str(TARGET_API)) / 'android.jar')
    if not os.environ.get('JAVA_HOME') and sys.platform == 'darwin':
        os.environ['JAVA_HOME'] = subprocess.check_output(['/usr/libexec/java_home', '-v', '17'], text=True).strip()
    if os.environ.get('JAVA_HOME'):
        os.environ['PATH'] = str(Path(os.environ['JAVA_HOME']) / 'bin') + os.pathsep + os.environ['PATH']
    for command in ['cmake', 'ninja', 'javac', 'keytool', 'curl']:
        if not shutil.which(command): raise RuntimeError('Install build prerequisite: ' + command)
    build = ROOT / 'build-android'; deps = build / 'deps'; deps.mkdir(parents=True, exist_ok=True)
    sdl = source(deps, 'SDL3-' + SDL_VERSION, '.tar.gz',
                 'https://github.com/libsdl-org/SDL/releases/download/release-' + SDL_VERSION + '/SDL3-' + SDL_VERSION + '.tar.gz', SDL_SHA)
    ft = source(deps, 'freetype-' + FT_VERSION, '.tar.xz',
                'https://download.savannah.gnu.org/releases/freetype/freetype-' + FT_VERSION + '.tar.xz', FT_SHA)
    flags = ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
             '-DCMAKE_TOOLCHAIN_FILE=' + str(ndk / 'build/cmake/android.toolchain.cmake'),
             '-DANDROID_ABI=' + ABI, '-DANDROID_PLATFORM=android-' + str(MIN_API),
             '-DANDROID_STL=c++_static', '-DCMAKE_POSITION_INDEPENDENT_CODE=ON']
    sdl_install, ft_install = build / 'sdl-install', build / 'freetype-install'
    print('Building SDL and FreeType for Android ARM64', flush=True)
    run(['cmake', '-S', sdl, '-B', build / 'sdl', *flags, '-DSDL_SHARED=ON', '-DSDL_STATIC=OFF',
         '-DSDL_TEST_LIBRARY=OFF', '-DSDL_EXAMPLES=OFF', '-DCMAKE_INSTALL_PREFIX=' + str(sdl_install)])
    run(['cmake', '--build', build / 'sdl', '-j', args.jobs]); run(['cmake', '--install', build / 'sdl'])
    run(['cmake', '-S', ft, '-B', build / 'freetype', *flags, '-DBUILD_SHARED_LIBS=OFF',
         '-DFT_DISABLE_PNG=ON', '-DFT_DISABLE_BZIP2=ON', '-DFT_DISABLE_HARFBUZZ=ON',
         '-DFT_DISABLE_BROTLI=ON', '-DFT_REQUIRE_ZLIB=ON', '-DCMAKE_INSTALL_PREFIX=' + str(ft_install)])
    run(['cmake', '--build', build / 'freetype', '-j', args.jobs]); run(['cmake', '--install', build / 'freetype'])
    print('Building Reprise', flush=True)
    roots = str(sdl_install) + ';' + str(ft_install)
    run(['cmake', '-S', ROOT, '-B', build / 'app', *flags, '-DBUILD_TESTING=OFF',
         '-DCMAKE_FIND_ROOT_PATH=' + roots, '-DCMAKE_PREFIX_PATH=' + roots,
         '-DSDL3_DIR=' + str(sdl_install / 'lib/cmake/SDL3'),
         '-DFREETYPE_INCLUDE=' + str(ft_install / 'include/freetype2'),
         '-DFREETYPE_STATIC=' + str(ft_install / 'lib/libfreetype.a')])
    run(['cmake', '--build', build / 'app', '--target', 'reprise', '-j', args.jobs])
    java, dex, apk = build / 'java', build / 'dex', build / 'apk'
    for folder in [java, dex, apk]:
        if folder.exists(): shutil.rmtree(folder)
        folder.mkdir()
    sources = sorted((sdl / 'android-project/app/src/main/java').rglob('*.java')) + sorted((ROOT / 'targets/android/java').rglob('*.java'))
    # Do not swallow javac/d8 errors: never package stale bytecode after failure.
    run(['javac', '-encoding', 'UTF-8', '-source', '8', '-target', '8', '-bootclasspath', android_jar, '-d', java, *sources])
    run([tools / 'd8', '--release', '--min-api', MIN_API, '--lib', android_jar, '--output', dex, *sorted(java.rglob('*.class'))])
    android = ROOT / 'targets/android'
    assets = apk / 'assets'
    shutil.copytree(ROOT / 'resources', assets)
    notices = assets / 'licenses'; notices.mkdir(exist_ok=True)
    shutil.copy2(sdl / 'LICENSE.txt', notices / 'SDL.txt')
    shutil.copy2(ft / 'docs/FTL.TXT', notices / 'FreeType.txt')
    shutil.copy2(ft / 'LICENSE.TXT', notices / 'FreeType-license-selection.txt')
    version = re.search(r'project\(Reprise VERSION ([\d.]+)', (ROOT / 'CMakeLists.txt').read_text()).group(1)
    version_name = version if args.release else version + '-dev'
    run([tools / 'aapt2', 'compile', '--dir', android / 'res', '-o', apk / 'res.zip'])
    unaligned = apk / 'unaligned.apk'
    run([tools / 'aapt2', 'link', '-o', unaligned, '-I', android_jar, '--manifest', android / 'AndroidManifest.xml',
         '--min-sdk-version', MIN_API, '--target-sdk-version', TARGET_API, '--version-code', args.version_code,
         '--version-name', version_name, '-A', assets, apk / 'res.zip'])
    binaries = next((ndk / 'toolchains/llvm/prebuilt').glob('*/bin'))
    libs = apk / 'lib' / ABI; libs.mkdir(parents=True)
    for name, original in [('libSDL3.so', sdl_install / 'lib/libSDL3.so'), ('libmain.so', build / 'app/libmain.so')]:
        output = libs / name
        run([binaries / 'llvm-strip', '--strip-unneeded', '-o', output, original])
        headers = subprocess.check_output([str(binaries / 'llvm-readelf'), '-lW', str(output)], text=True)
        loads = [line.split() for line in headers.splitlines() if line.strip().startswith('LOAD')]
        if not loads or any(int(line[-1], 16) < 16384 for line in loads):
            raise RuntimeError('Native library is not 16 KB aligned: ' + name)
    with zipfile.ZipFile(unaligned, 'a') as package:
        for f in sorted(dex.glob('*.dex')): package.write(f, f.name, compress_type=zipfile.ZIP_DEFLATED)
        for f in sorted(libs.glob('*.so')): package.write(f, 'lib/' + ABI + '/' + f.name, compress_type=zipfile.ZIP_STORED)
    aligned = apk / 'aligned.apk'; result = build / 'Reprise.apk'
    run([tools / 'zipalign', '-f', '-P', '16', '4', unaligned, aligned])
    key = Path.home() / '.android/debug.keystore'
    if not key.exists():
        key.parent.mkdir(parents=True, exist_ok=True)
        run(['keytool', '-genkeypair', '-keystore', key, '-storepass', 'android', '-alias', 'androiddebugkey',
             '-keypass', 'android', '-dname', 'CN=Android Debug,O=Android,C=US', '-keyalg', 'RSA', '-keysize', '2048', '-validity', '10000'])
    run([tools / 'apksigner', 'sign', '--ks', key, '--ks-pass', 'pass:android', '--ks-key-alias', 'androiddebugkey', '--out', result, aligned])
    run([tools / 'apksigner', 'verify', '--verbose', result])
    run([tools / 'zipalign', '-c', '-P', '16', '4', result])
    with zipfile.ZipFile(result) as package:
        forbidden = {'.ipa', '.nds', '.3ds', '.cia', '.adf', '.tap', '.d64', '.sav', '.keystore'}
        if any(Path(n).suffix.lower() in forbidden for n in package.namelist()):
            raise RuntimeError('Source game data or signing material found in APK')
    (build / 'build-info.json').write_text(json.dumps({'version': version_name, 'version_code': args.version_code,
        'abi': ABI, 'min_sdk': MIN_API, 'target_sdk': TARGET_API, 'ndk': str(ndk), 'sdl': SDL_VERSION,
        'freetype': FT_VERSION, 'apk_sha256': digest(result)}, indent=2) + '\n')
    print('Built ' + str(result) + ' (' + str(round(result.stat().st_size / 1048576, 1)) + ' MiB)', flush=True)
    if args.install:
        adb = [sdk / 'platform-tools/adb']
        if args.serial: adb += ['-s', args.serial]
        run([*adb, 'install', '-r', result])

if __name__ == '__main__':
    try: main()
    except (RuntimeError, subprocess.CalledProcessError) as e:
        print('Android build failed: ' + str(e), file=sys.stderr)
        sys.exit(1)
