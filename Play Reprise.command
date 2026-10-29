#!/bin/zsh
# Double-click launcher: builds Reprise on first use, then starts it.
set -eu
cd -- "$(dirname -- "$0")"
app='build/Reprise.app/Contents/MacOS/reprise'
if [[ ! -x "$app" ]]; then
    ./build.sh
fi
exec "$app"
