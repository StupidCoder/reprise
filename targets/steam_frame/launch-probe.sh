#!/bin/sh
set -eu
probe_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
log_dir=${XDG_STATE_HOME:-"$HOME/.local/state"}/reprise
mkdir -p "$log_dir"
log="$log_dir/steam-frame-probe-$(date -u +%Y%m%dT%H%M%SZ)-$$.log"
ln -sfn "$(basename "$log")" "$log_dir/steam-frame-probe.log"
exec "$probe_dir/reprise-frame-probe" "$@" >"$log" 2>&1
