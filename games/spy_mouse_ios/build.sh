#!/bin/zsh
# Builds DoubleOCheese (optimised, with debug symbols): build/doubleocheese (the launcher and the
# game), build/doc-verify (compares the reconstruction with reference traces of the original)
# and, on macOS, the self-contained app bundle build/DoubleOCheese.app with SDL bundled. The
# bundle contains code and its own icon only: the game is imported from the user's IPA at run
# time. Requires CMake, Ninja, SDL 3 and zlib (for example from Homebrew); the icon needs Pillow.
set -eu
cd -- "$(dirname -- "$0")"
export PATH="/opt/homebrew/bin:$PATH"
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/opt/homebrew > /dev/null
cmake --build build
if [[ "$(uname)" == Darwin ]]; then
    [[ -f build/AppIcon.icns ]] || python3 tools/make_icon.py > /dev/null || echo "no icon (Pillow missing)"
    python3 tools/package_macos.py
fi
