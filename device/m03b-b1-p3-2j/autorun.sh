#!/bin/sh
# NEUROGENIC B1-P3.2j: bounded marker-only autorun diagnostic.
set -u

[ "$#" -eq 0 ] || exit 1
umask 077
PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PATH
unset CDPATH ENV BASH_ENV LD_PRELOAD LD_LIBRARY_PATH

case "$0" in /media/*/autorun/autorun.sh) ;; *) exit 1 ;; esac
RAW_DIR=${0%/autorun.sh}
SD=${RAW_DIR%/autorun}
case "$SD" in /media/*) ;; *) exit 1 ;; esac
component=${SD#/media/}
[ -n "$component" ] && [ "$component" != "$SD" ] || exit 1
case "$component" in */*|*'%'*|*'\'*) exit 1 ;; esac

# Match one exact writable exFAT SD record. /proc/mounts encodes spaces as \040.
awk -v p="$SD" '
BEGIN { gsub(/ /, sprintf("%c040", 92), p) }
$1 == "/dev/mmcblk1p1" {
    rows++
    if ($2 == p && $3 == "exfat" && $4 ~ /(^|,)rw(,|$)/ && $4 !~ /(^|,)ro(,|$)/) ok++
}
END { exit !(rows == 1 && ok == 1) }
' /proc/mounts || exit 1
[ "$(readlink -f "$SD")" = "$SD" ] || exit 1

MARKER_DIR="$SD/b1-p3-2j-startup"
PARTIAL="$MARKER_DIR/marker.partial"
FINAL="$MARKER_DIR/marker.txt"
mkdir "$MARKER_DIR" || exit 1
[ -d "$MARKER_DIR" ] && [ ! -L "$MARKER_DIR" ] || exit 1
[ ! -e "$PARTIAL" ] && [ ! -e "$FINAL" ] || exit 1

directory_valid=unknown
firmware_guard=not-reached
write_marker() {
    printf '%s\n' \
        'schema=neurogenic-b1-startup-v1' \
        'autorun_entered=true' \
        "directory_valid=$directory_valid" \
        "firmware_guard=$firmware_guard" \
        'collector_invocation=deferred' > "$PARTIAL"
}
finish_marker() {
    write_marker || exit 1
    sync "$PARTIAL" || exit 1
    mv "$PARTIAL" "$FINAL" || exit 1
    sync "$MARKER_DIR" || exit 1
    exit 0
}

write_marker || exit 1
if ! DIR=$(CDPATH= cd -- "$RAW_DIR" && pwd); then
    directory_valid=false
    finish_marker
fi
if [ "$DIR" != "$RAW_DIR" ]; then
    directory_valid=false
    finish_marker
fi
directory_valid=true
write_marker || exit 1

STOCK="/usr/bin/MPC Sample"
FW="44507c7a7d3029c411b2e3780ea286ca3e08b7e3ffd7b5047d52f73bf5299d1f"
if [ ! -f "$STOCK" ] || [ -L "$STOCK" ]; then
    firmware_guard=fail
    finish_marker
fi
READ_CHECK=$(
    exec 3>&1
    { dd if="$STOCK" bs=4096 count=1 2>/dev/null; printf 'READ_STATUS=%s\n' "$?" >&3; } |
        { bytes=$(wc -c); status=$?; printf 'BYTES=%s\nCOUNT_STATUS=%s\n' "$bytes" "$status" >&3; }
)
if [ "$READ_CHECK" != "READ_STATUS=0
BYTES=4096
COUNT_STATUS=0" ]; then
    firmware_guard=fail
    finish_marker
fi
HASH_CHECK=$(
    exec 3>&1
    { dd if="$STOCK" bs=4096 count=1 2>/dev/null; printf 'READ_STATUS=%s\n' "$?" >&3; } |
        { output=$(sha256sum); status=$?; printf 'HASH_STATUS=%s\nHASH_OUTPUT=%s\n' "$status" "$output" >&3; }
)
if [ "$HASH_CHECK" = "READ_STATUS=0
HASH_STATUS=0
HASH_OUTPUT=$FW  -" ]; then
    firmware_guard=pass
else
    firmware_guard=fail
fi
finish_marker
