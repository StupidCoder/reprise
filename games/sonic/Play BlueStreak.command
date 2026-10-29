#!/bin/zsh
# Double-click launcher: builds BlueStreak on first use, then starts it.
set -eu
cd -- "$(dirname -- "$0")"
app='build/BlueStreak.app/Contents/MacOS/bluestreak'
if [[ ! -x "$app" ]]; then
    ./build.sh
fi
exec "$app"
