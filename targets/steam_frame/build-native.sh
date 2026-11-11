#!/bin/bash
# Run on the headset via dev.py. No root access or OS package modifications.
set -euo pipefail
source_dir=$(cd -- "$(dirname -- "$0")" && pwd)
work="$HOME/reprise-dev"
headers="$work/deps/include/openxr"
mkdir -p "$headers"
for name in openxr.h openxr_platform.h openxr_platform_defines.h; do
    if [[ ! -s "$headers/$name" ]]; then
        curl --retry 2 --connect-timeout 10 --max-time 30 -fsSL \
            "https://raw.githubusercontent.com/KhronosGroup/OpenXR-SDK/release-1.1.53/include/openxr/$name" \
            -o "$headers/$name.tmp"
        mv "$headers/$name.tmp" "$headers/$name"
    fi
done
(cd "$headers" && sha256sum -c "$source_dir/openxr-headers.sha256")
build_dir="$work/build-native"
if [[ "${1:-OFF}" == LAUNCHER ]]; then
    build_dir="$work/build-launcher"
    cmake -S "$source_dir/../.." -B "$build_dir" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
        -DREPRISE_STEAM_FRAME_LAUNCHER=ON \
        -DOPENXR_INCLUDE_DIR="$work/deps/include" \
        -DOPENXR_LOADER=/opt/steamvr/bin/linuxarm64/libopenxr_loader.so
    cmake --build "$build_dir" --target reprise-frame-probe --parallel 4
else
cmake -S "$source_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DOPENXR_INCLUDE_DIR="$work/deps/include" \
    -DOPENXR_LOADER=/opt/steamvr/bin/linuxarm64/libopenxr_loader.so \
    -DREPRISE_FRAME_GAME_BRIDGE="${1:-OFF}"
cmake --build "$build_dir" --parallel 4
ctest --test-dir "$build_dir" --output-on-failure
fi
cmake --install "$build_dir" --component SteamFrame --prefix "$work/bundle"
{
    date -u; uname -a; cat /etc/os-release
    c++ --version; cmake --version
    pkg-config --modversion sdl3 vulkan
    sha256sum "$headers/"*.h "$work/bundle/reprise-frame-probe"
    sha256sum "$source_dir/"*.cpp "$source_dir/"*.hpp "$source_dir/CMakeLists.txt"
    readelf -h -d -V "$work/bundle/reprise-frame-probe"
    cat "$HOME/.local/share/Steam/steamapps/common/SteamLinuxRuntime_4-arm64/VERSIONS.txt"
} > "$work/bundle/build-manifest.txt"
"$HOME/.local/share/Steam/steamapps/common/SteamLinuxRuntime_4-arm64/run" -- \
    ldd "$work/bundle/reprise-frame-probe" > "$work/bundle/runtime-linkage.txt"
cat "$work/bundle/runtime-linkage.txt"
! grep -q 'not found' "$work/bundle/runtime-linkage.txt"
