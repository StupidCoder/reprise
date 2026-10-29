"""Packages build/doubleocheese as a self-contained macOS app bundle, build/DoubleOCheese.app.

The bundle contains the executable, the libraries it links that macOS does not provide (SDL 3,
with its license) and the app icon; no game data. The launcher imports the game from the user's
own IPA at run time. It is signed ad hoc, which is enough to run locally but not notarized for
distribution.
"""
import plistlib
import re
import shutil
import stat
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
# The version from CMakeLists.txt (project(DoubleOCheese VERSION x.y.z ...)); BUILD counts bundles.
VERSION = re.search(r'project\(DoubleOCheese VERSION ([0-9.]+)', (root / 'CMakeLists.txt').read_text()).group(1)
BUILD = '1'
destination = root / 'build' / 'DoubleOCheese.app'


def run(*args):
    return subprocess.run([str(a) for a in args], check=True, capture_output=True, text=True).stdout


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

    executable = contents / 'MacOS' / 'doubleocheese'
    shutil.copy2(root / 'build' / 'doubleocheese', executable)
    info = {
        'CFBundleName': 'DoubleOCheese',
        'CFBundleDisplayName': 'DoubleOCheese',
        'CFBundleIdentifier': 'local.reconstruction.doubleocheese',
        'CFBundleExecutable': 'doubleocheese',
        'CFBundlePackageType': 'APPL',
        'CFBundleVersion': BUILD,
        'CFBundleShortVersionString': VERSION,
        'LSMinimumSystemVersion': '13.3',
        'NSHighResolutionCapable': True,
        'NSHumanReadableCopyright': 'DoubleOCheese is an independent project, not affiliated with the game\'s publishers.',
        # Lets the user drop an .ipa on the app icon (opened with the file as an argument).
        'CFBundleDocumentTypes': [{
            'CFBundleTypeName': 'iPhone app archive',
            'CFBundleTypeExtensions': ['ipa'],
            'CFBundleTypeRole': 'Viewer',
            'LSHandlerRank': 'Alternate',
        }],
    }
    icon = root / 'build' / 'AppIcon.icns'
    if icon.exists():
        shutil.copy2(icon, contents / 'Resources' / 'AppIcon.icns')
        info['CFBundleIconFile'] = 'AppIcon'
    (contents / 'Info.plist').write_bytes(plistlib.dumps(info))

    # Bundle every non-system library the executable needs (transitively) and point each binary
    # at the bundled copies.
    frameworks = contents / 'Frameworks'
    bundled = {}   # original path -> bundled path

    def bundle_library(path):
        real = Path(path).resolve()
        if path in bundled:
            return bundled[path]
        target = frameworks / Path(path).name
        if not target.exists():
            shutil.copy2(real, target)
            target.chmod(target.stat().st_mode | stat.S_IWUSR)
            run('install_name_tool', '-id', '@rpath/' + target.name, target)
        bundled[path] = target
        for dependency in linked_libraries(target)[1:]:
            if not is_system(dependency) and not dependency.startswith('@'):
                bundle_library(dependency)
                run('install_name_tool', '-change', dependency, '@loader_path/' + Path(dependency).name, target)
        return target

    for library in linked_libraries(executable):
        if not is_system(library) and not library.startswith('@'):
            bundle_library(library)
            run('install_name_tool', '-change', library, '@executable_path/../Frameworks/' + Path(library).name,
                executable)
    shutil.copy2(Path('/opt/homebrew/opt/sdl3/LICENSE.txt'), contents / 'Resources/licenses' / 'SDL.txt')

    libraries = sorted(frameworks.glob('*.dylib'))
    for binary in libraries + [executable]:
        run('codesign', '--force', '--sign', '-', binary)
    run('codesign', '--force', '--sign', '-', bundle)
    run('codesign', '--verify', '--deep', '--strict', bundle)

    # The bundle must be self-contained and must not contain any game data.
    for pattern in ['*.ipa', '*.mbn', '*.brd', '*.png', '*.jpg', '*.caf', '*.m4a', 'SpyMouse']:
        assert not list(bundle.rglob(pattern)), pattern
    for binary in [executable] + libraries:
        assert all(is_system(p) or p.startswith('@') for p in linked_libraries(binary)), binary

    if destination.exists():
        destination.rename(Path(temp) / 'previous.app')  # Removed with the temp folder.
    bundle.rename(destination)

print('Packaged:', destination)
