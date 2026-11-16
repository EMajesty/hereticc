#!/bin/sh
# NEUROGENIC B1-P3.2g: fixed, one-shot, zero-hook diagnostic entry point.
set -u

[ "$#" -eq 0 ] || exit 64
umask 077
PATH=/usr/bin:/bin
export PATH
unset CDPATH ENV BASH_ENV LD_PRELOAD LD_LIBRARY_PATH

DIR="$(cd "$(dirname "$0")" && pwd)" || exit 1
case "$DIR" in
    /media/*/autorun) ;;
    *) exit 1 ;;
esac

STOCK="/usr/bin/MPC Sample"
FW="44507c7a7d3029c411b2e3780ea286ca3e08b7e3ffd7b5047d52f73bf5299d1f"
COLLECTOR="$DIR/p3_2f_collector"

[ -f "$STOCK" ] && [ ! -L "$STOCK" ] || exit 1
[ "$(dd if="$STOCK" bs=4096 count=1 2>/dev/null | sha256sum | cut -d" " -f1)" = "$FW" ] || exit 1
[ -f "$COLLECTOR" ] && [ -x "$COLLECTOR" ] && [ ! -L "$COLLECTOR" ] || exit 1

exec "$COLLECTOR"
