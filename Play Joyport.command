#!/bin/zsh
# Double-click launcher: builds Joyport on first use, then starts it.
set -eu
cd -- "$(dirname -- "$0")"
app='build/Joyport.app/Contents/MacOS/joyport'
if [[ ! -x "$app" ]]; then
    ./build.sh
fi
exec "$app"
