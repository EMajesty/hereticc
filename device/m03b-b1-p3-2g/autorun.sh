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
READ_CHECK=$(
    exec 3>&1
    { dd if="$STOCK" bs=4096 count=1 2>/dev/null; printf 'READ_STATUS=%s\n' "$?" >&3; } |
        { bytes=$(wc -c); status=$?; printf 'BYTES=%s\nCOUNT_STATUS=%s\n' "$bytes" "$status" >&3; }
)
[ "$READ_CHECK" = "READ_STATUS=0
BYTES=4096
COUNT_STATUS=0" ] || exit 1

HASH_CHECK=$(
    exec 3>&1
    { dd if="$STOCK" bs=4096 count=1 2>/dev/null; printf 'READ_STATUS=%s\n' "$?" >&3; } |
        { output=$(sha256sum); status=$?; printf 'HASH_STATUS=%s\nHASH_OUTPUT=%s\n' "$status" "$output" >&3; }
)
[ "$HASH_CHECK" = "READ_STATUS=0
HASH_STATUS=0
HASH_OUTPUT=$FW  -" ] || exit 1
[ -f "$COLLECTOR" ] && [ -x "$COLLECTOR" ] && [ ! -L "$COLLECTOR" ] || exit 1

exec "$COLLECTOR"
