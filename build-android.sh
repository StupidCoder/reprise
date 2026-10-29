#!/bin/sh
# Repeatable arm64 Android test build; SDK, NDK and JDK required.
set -eu
cd "$(dirname "$0")"
exec python3 tools/build_android.py "$@"
