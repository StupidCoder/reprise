#!/bin/zsh
# Builds Joyport (optimised, with debug symbols) and packages it as
# "build/Joyport.app" with SDL bundled. Requires CMake, Ninja and SDL 3
# (for example from Homebrew).
set -eu
cd -- "$(dirname -- "$0")"
export PATH="/opt/homebrew/bin:$PATH"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build
python3 tools/package_macos.py
