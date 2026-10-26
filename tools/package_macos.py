"""Packages build/joyport as a self-contained macOS app bundle.

The bundle contains the executable and the SDL 3 library it links (with its
license); no game data. It is signed ad hoc, which is enough to run locally
but not notarized for distribution.
"""
import plistlib
import shutil
import stat
import subprocess
import tempfile
from pathlib import Path

VERSION = '0.2.0'
BUILD = '1'

root = Path(__file__).resolve().parents[1]
destination = root / 'build' / 'Joyport.app'


def run(*args):
    return subprocess.run([str(a) for a in args], check=True, capture_output=True,
                          text=True).stdout


def linked_libraries(binary):
    """Install names of the dynamic libraries a binary links against."""
    return [line.strip().split(' (')[0] for line in run('otool', '-L', binary).splitlines()[1:]]


def is_system(path):
    return path.startswith(('/usr/lib/', '/System/Library/'))


with tempfile.TemporaryDirectory(prefix='package-', dir=root / 'build') as temp:
    bundle = Path(temp) / destination.name
    contents = bundle / 'Contents'
    for folder in ['MacOS', 'Frameworks', 'Resources/licenses']:
        (contents / folder).mkdir(parents=True, exist_ok=True)

    executable = contents / 'MacOS' / 'joyport'
    shutil.copy2(root / 'build' / 'joyport', executable)
    (contents / 'Info.plist').write_bytes(plistlib.dumps({
        'CFBundleName': 'Joyport',
        'CFBundleDisplayName': 'Joyport',
        'CFBundleIdentifier': 'local.joyport',
        'CFBundleExecutable': 'joyport',
        'CFBundlePackageType': 'APPL',
        'CFBundleVersion': BUILD,
        'CFBundleShortVersionString': VERSION,
        'NSHighResolutionCapable': True,
    }))

    # Original cover illustrations and redistributable interface fonts only.
    shutil.copytree(root / 'resources', contents / 'Resources', dirs_exist_ok=True)

    # Bundle SDL (the only non-system library) and point the executable at it.
    external = [p for p in linked_libraries(executable) if not is_system(p)]
    assert len(external) == 1 and Path(external[0]).name == 'libSDL3.0.dylib', external
    sdl = Path(external[0])
    bundled_sdl = contents / 'Frameworks' / sdl.name
    shutil.copy2(sdl, bundled_sdl)
    bundled_sdl.chmod(bundled_sdl.stat().st_mode | stat.S_IWUSR)
    shutil.copy2(sdl.parent.parent / 'LICENSE.txt', contents / 'Resources/licenses/SDL.txt')
    run('install_name_tool', '-change', sdl, '@executable_path/../Frameworks/' + sdl.name,
        executable)
    run('install_name_tool', '-id', '@rpath/' + sdl.name, bundled_sdl)

    for binary in [bundled_sdl, executable]:
        run('codesign', '--force', '--sign', '-', binary)
    run('codesign', '--force', '--sign', '-', bundle)
    run('codesign', '--verify', '--deep', '--strict', bundle)

    # The bundle must be self-contained and must not contain any game data.
    assert not list(bundle.rglob('*.tap')) and not list(bundle.rglob('*.prg'))
    for binary in [executable, bundled_sdl]:
        assert all(is_system(p) or p.startswith('@') for p in linked_libraries(binary))

    if destination.exists():
        destination.rename(Path(temp) / 'previous.app')  # Removed with the temp folder.
    bundle.rename(destination)

print('Packaged:', destination)
