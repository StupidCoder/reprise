#!/bin/zsh
# Builds Picarat for Android (64-bit ARM phones) into an APK for sideloading:
# build-android/Picarat.apk, copied to dist/v<version>/Picarat-<version>.apk.
#
#   ./build-android.sh            build
#   ./build-android.sh --install  build and install on the phone or emulator
#                                 that adb sees (USB debugging)
#
# Requires the Android SDK (platform, build-tools, NDK, platform-tools; found
# in $ANDROID_HOME or ~/Library/Android/sdk), a JDK, CMake and Ninja. SDL is
# built from its release source, which is downloaded once into
# build-android/deps. No Gradle: the SDK's own tools (aapt2, d8, zipalign,
# apksigner) assemble the APK. It is signed with the SDK's debug key
# (~/.android/debug.keystore), so later builds install over earlier ones.
set -eu
cd -- "$(dirname -- "$0")"
export PATH="/opt/homebrew/bin:$PATH"

install=0
for arg in "$@"; do
  case $arg in
    --install) install=1 ;;
    *) echo "usage: $0 [--install]" >&2; exit 2 ;;
  esac
done

sdk=${ANDROID_HOME:-$HOME/Library/Android/sdk}
latest() { ls -1 $1 | sort -V | tail -n 1 }
ndk=$sdk/ndk/$(latest $sdk/ndk)
tools=$sdk/build-tools/$(latest $sdk/build-tools)
platform=$(latest $sdk/platforms)
androidJar=$sdk/platforms/$platform/android.jar
targetSdk=${platform#android-}
targetSdk=${targetSdk%%.*}
minSdk=26
abi=arm64-v8a
build=$PWD/build-android

version=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' packaging/Info.plist.in)
versionCode=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' packaging/Info.plist.in)

# SDL's release source (C library and its Java activity).
sdlVersion=3.4.16
sdlSha256=7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68
sdl=$build/deps/SDL3-$sdlVersion
if [[ ! -e $sdl/CMakeLists.txt ]]; then
  mkdir -p $build/deps
  archive=$build/deps/SDL3-$sdlVersion.tar.gz
  curl -fsSL -o $archive \
    https://github.com/libsdl-org/SDL/releases/download/release-$sdlVersion/SDL3-$sdlVersion.tar.gz
  echo "$sdlSha256  $archive" | shasum -a 256 -c -
  tar -xzf $archive -C $build/deps
fi

ndkFlags=(
  -G Ninja -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_TOOLCHAIN_FILE=$ndk/build/cmake/android.toolchain.cmake
  -DANDROID_ABI=$abi -DANDROID_PLATFORM=android-$minSdk -DANDROID_STL=c++_static
)

# SDL as libSDL3.so.
if [[ ! -e $build/sdl-install/lib/libSDL3.so ]]; then
  cmake -S $sdl -B $build/sdl $ndkFlags -DSDL_SHARED=ON -DSDL_STATIC=OFF \
    -DSDL_TEST_LIBRARY=OFF -DSDL_EXAMPLES=OFF -DCMAKE_INSTALL_PREFIX=$build/sdl-install
  cmake --build $build/sdl
  cmake --install $build/sdl
fi

# The app as libmain.so.
cmake -S . -B $build/app $ndkFlags -DCMAKE_FIND_ROOT_PATH=$build/sdl-install \
  -DSDL3_DIR=$build/sdl-install/lib/cmake/SDL3
cmake --build $build/app --target picarat

# The Java activity (SDL's and Picarat's subclass), compiled to Dalvik bytecode.
rm -rf $build/java $build/dex && mkdir -p $build/java $build/dex
javac -nowarn -Xlint:none -source 8 -target 8 -bootclasspath $androidJar \
  -d $build/java $(find $sdl/android-project/app/src/main/java android/java -name '*.java') 2>&1 |
  grep -v '^warning: \[options\]\|^Note: \|^[0-9]* warning' || true
$tools/d8 --release --min-api $minSdk --lib $androidJar --output $build/dex \
  $(find $build/java -name '*.class')

# Package: resources and manifest, artwork as assets, code, native libraries.
rm -rf $build/apk && mkdir -p $build/apk/assets/art $build/apk/lib/$abi
cp art/*.png $build/apk/assets/art/
cp -R resources/fonts resources/licenses $build/apk/assets/
$tools/aapt2 compile --dir android/res -o $build/apk/res.zip
$tools/aapt2 link -o $build/apk/unaligned.apk -I $androidJar \
  --manifest android/AndroidManifest.xml --min-sdk-version $minSdk \
  --target-sdk-version $targetSdk --version-code $versionCode --version-name $version \
  -A $build/apk/assets $build/apk/res.zip
llvmStrip=$(echo $ndk/toolchains/llvm/prebuilt/*/bin/llvm-strip)
$llvmStrip --strip-unneeded -o $build/apk/lib/$abi/libSDL3.so $build/sdl-install/lib/libSDL3.so
$llvmStrip --strip-unneeded -o $build/apk/lib/$abi/libmain.so $build/app/libmain.so
(cd $build/dex && zip -q -X ../apk/unaligned.apk classes.dex)
# Native libraries stay uncompressed so Android can map them from the APK.
(cd $build/apk && zip -q -X -0 unaligned.apk lib/$abi/libSDL3.so lib/$abi/libmain.so)
$tools/zipalign -f -P 16 4 $build/apk/unaligned.apk $build/apk/aligned.apk
$tools/apksigner sign --ks $HOME/.android/debug.keystore --ks-pass pass:android \
  --ks-key-alias androiddebugkey --out $build/Picarat.apk $build/apk/aligned.apk
$tools/apksigner verify $build/Picarat.apk

if [[ -n "$(unzip -l $build/Picarat.apk | grep -i -e '\.nds' -e '\.sav')" ]]; then
  echo "game data found in the APK" >&2
  exit 1
fi

mkdir -p dist/v$version
cp $build/Picarat.apk dist/v$version/Picarat-$version.apk
echo "built dist/v$version/Picarat-$version.apk ($(du -h $build/Picarat.apk | cut -f1))"

if (( install )); then
  $sdk/platform-tools/adb install -r $build/Picarat.apk
fi
