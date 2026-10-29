#!/bin/zsh
# Builds Picarat and runs the tests. On macOS it also packages an asset-free
# "dist/v<version>/Picarat.app" (with SDL bundled) plus a ZIP.
# Requires CMake, Ninja, zlib and SDL 3.4 (macOS: Xcode command-line tools
# and "brew install cmake ninja sdl3").
# If build-release/native-assets holds a test pack (picarat_import --pack),
# the data tests and the app's story/first-puzzle self-test run as well.
set -eu
cd -- "$(dirname -- "$0")"
export PATH="/opt/homebrew/bin:$PATH"
build=build-release

cmake -S . -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build $build
ctest --test-dir $build --output-on-failure

if [[ "$(uname)" != Darwin ]]; then
  if [[ -e $build/native-assets/provenance.json ]]; then
    rm -rf $build/self-test
    $build/picarat --self-test $build/self-test --assets $build/native-assets
  fi
  exit 0
fi

version=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' packaging/Info.plist.in)
out=dist/v$version
if [[ -e $out/Picarat.app ]]; then
  echo "$out/Picarat.app already exists; earlier releases are kept. Bump the version in packaging/Info.plist.in." >&2
  exit 1
fi
mkdir -p $out
app=$out/Picarat.app
ditto $build/Picarat.app $app
exe=$app/Contents/MacOS/Picarat

# Bundle SDL, the only library outside the system, and point the app at it.
sdl=$(otool -L $exe | awk '/libSDL3/ {print $1}')
mkdir -p $app/Contents/Frameworks $app/Contents/Resources/licenses
cp $sdl $app/Contents/Frameworks/
chmod u+w $app/Contents/Frameworks/${sdl:t}
install_name_tool -change $sdl @executable_path/../Frameworks/${sdl:t} $exe
install_name_tool -id @rpath/${sdl:t} $app/Contents/Frameworks/${sdl:t}
cp ${sdl:h:h}/LICENSE.txt $app/Contents/Resources/licenses/SDL.txt
cp LICENSE $app/Contents/Resources/licenses/Picarat.txt
if otool -L $exe $app/Contents/Frameworks/${sdl:t} | tail -n +2 | grep -v ':$' |
    grep -v -e '/usr/lib/' -e '/System/Library/' -e '@'; then
  echo "the bundle links libraries outside the system" >&2
  exit 1
fi

if [[ -n "$(find $app -name '*.nds' -o -name Assets)" ]]; then
  echo "game data found in the bundle" >&2
  exit 1
fi
# Copied resources may carry Finder info or quarantine attributes, which
# codesign rejects.
xattr -cr $app
codesign --force --sign - $app/Contents/Frameworks/${sdl:t}
codesign --force --deep --sign - $app
codesign --verify --deep --strict $app
if [[ -e $build/native-assets/provenance.json ]]; then
  rm -rf $build/self-test
  $exe --self-test $build/self-test --assets $build/native-assets
fi
# ditto preserves the build directory's original dates. Finder displays the
# outer bundle's dates, so stamp the finished release before archiving it.
touch "$app"
if command -v SetFile >/dev/null 2>&1; then
  SetFile -d "$(date '+%m/%d/%Y %H:%M:%S')" "$app"
fi
ditto -c -k --sequesterRsrc --keepParent $app $out/Picarat.zip
shasum -a 256 $exe $out/Picarat.zip
