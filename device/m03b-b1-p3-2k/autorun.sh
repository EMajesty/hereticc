#!/bin/sh
# NEUROGENIC B1-P3.2k: one-shot RAM-staged zero-hook lifecycle diagnostic.
set -u

[ "$#" -eq 0 ] || exit 64
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

awk -v p="$SD" '
BEGIN { gsub(/ /, sprintf("%c040", 92), p) }
$1 == "/dev/mmcblk1p1" {
    rows++
    if ($2 == p && $3 == "exfat" && $4 ~ /(^|,)rw(,|$)/ && $4 !~ /(^|,)ro(,|$)/) ok++
}
END { exit !(rows == 1 && ok == 1) }
' /proc/mounts || exit 1
[ "$(readlink -f "$SD")" = "$SD" ] || exit 1

MARKER_DIR="$SD/b1-p3-2k-startup"
PARTIAL="$MARKER_DIR/marker.partial"
FINAL="$MARKER_DIR/marker.txt"
REPORT="$SD/b1-p3-2c-report.txt"
REPORT_PARTIAL="$SD/.b1-p3-2c-report.partial"
mkdir "$MARKER_DIR" || exit 1
[ -d "$MARKER_DIR" ] && [ ! -L "$MARKER_DIR" ] || exit 1
[ ! -e "$PARTIAL" ] && [ ! -e "$FINAL" ] || exit 1

firmware_guard=not-reached
collector_file=not-reached
ram_staging=not-reached
collector_invocation=not-reached
collector_exit=not-run
report_publication=not-checked
cleanup=not-reached
write_marker() {
    printf '%s\n' \
        'schema=neurogenic-b1-ram-stage-v1' \
        'autorun_entered=true' \
        "firmware_guard=$firmware_guard" \
        "collector_file=$collector_file" \
        "ram_staging=$ram_staging" \
        "collector_invocation=$collector_invocation" \
        "collector_exit=$collector_exit" \
        "report_publication=$report_publication" \
        "cleanup=$cleanup" > "$PARTIAL"
}
finish_marker() {
    write_marker || return 1
    sync "$PARTIAL" || return 1
    mv "$PARTIAL" "$FINAL" || return 1
    sync "$MARKER_DIR" || return 1
}
fail_before_collector() {
    finish_marker
    exit 1
}

write_marker || exit 1
if ! DIR=$(CDPATH= cd -- "$RAW_DIR" && pwd); then
    collector_file=fail
    fail_before_collector
fi
[ "$DIR" = "$RAW_DIR" ] || { collector_file=fail; fail_before_collector; }

STOCK="/usr/bin/MPC Sample"
FW="44507c7a7d3029c411b2e3780ea286ca3e08b7e3ffd7b5047d52f73bf5299d1f"
if [ ! -f "$STOCK" ] || [ -L "$STOCK" ]; then
    firmware_guard=fail
    fail_before_collector
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
    fail_before_collector
fi
HASH_CHECK=$(
    exec 3>&1
    { dd if="$STOCK" bs=4096 count=1 2>/dev/null; printf 'READ_STATUS=%s\n' "$?" >&3; } |
        { output=$(sha256sum); status=$?; printf 'HASH_STATUS=%s\nHASH_OUTPUT=%s\n' "$status" "$output" >&3; }
)
if [ "$HASH_CHECK" != "READ_STATUS=0
HASH_STATUS=0
HASH_OUTPUT=$FW  -" ]; then
    firmware_guard=fail
    fail_before_collector
fi
firmware_guard=pass
write_marker || exit 1

COLLECTOR="$DIR/p3_2f_collector"
COLLECTOR_HASH="58c0caa61a543203e4d34568b1011339a06e6fc6ea06d52d191828dc6dadc49c"
if [ ! -f "$COLLECTOR" ] || [ -L "$COLLECTOR" ]; then
    collector_file=fail
    fail_before_collector
fi
COLLECTOR_CHECK=$(sha256sum "$COLLECTOR" 2>/dev/null) || {
    collector_file=fail
    fail_before_collector
}
[ "$COLLECTOR_CHECK" = "$COLLECTOR_HASH  $COLLECTOR" ] || {
    collector_file=fail
    fail_before_collector
}
collector_file=pass
write_marker || exit 1

if [ -e "$REPORT" ] || [ -L "$REPORT" ] || [ -e "$REPORT_PARTIAL" ] || [ -L "$REPORT_PARTIAL" ]; then
    report_publication=blocked-existing
    fail_before_collector
fi

STAGE_DIR="/run/neurogenic-b1-p3-2k"
STAGED="$STAGE_DIR/p3_2f_collector"
mkdir "$STAGE_DIR" || { ram_staging=fail; fail_before_collector; }
[ -d "$STAGE_DIR" ] && [ ! -L "$STAGE_DIR" ] || {
    ram_staging=fail
    fail_before_collector
}
if ! cp "$COLLECTOR" "$STAGED"; then
    ram_staging=fail
    rm -f "$STAGED" 2>/dev/null
    rmdir "$STAGE_DIR" 2>/dev/null
    fail_before_collector
fi
if ! chmod 700 "$STAGED" || [ ! -f "$STAGED" ] || [ ! -x "$STAGED" ] || [ -L "$STAGED" ]; then
    ram_staging=fail
    rm -f "$STAGED" 2>/dev/null
    rmdir "$STAGE_DIR" 2>/dev/null
    fail_before_collector
fi
STAGED_CHECK=$(sha256sum "$STAGED" 2>/dev/null) || {
    ram_staging=fail
    rm -f "$STAGED" 2>/dev/null
    rmdir "$STAGE_DIR" 2>/dev/null
    fail_before_collector
}
if [ "$STAGED_CHECK" != "$COLLECTOR_HASH  $STAGED" ]; then
    ram_staging=fail
    rm -f "$STAGED" 2>/dev/null
    rmdir "$STAGE_DIR" 2>/dev/null
    fail_before_collector
fi
ram_staging=pass
collector_invocation=attempted
if ! write_marker; then
    rm -f "$STAGED" 2>/dev/null
    rmdir "$STAGE_DIR" 2>/dev/null
    exit 1
fi

"$STAGED"
collector_status=$?
collector_exit=$collector_status
if [ -f "$REPORT" ] && [ ! -L "$REPORT" ]; then
    report_publication=present
elif [ -e "$REPORT_PARTIAL" ] || [ -L "$REPORT_PARTIAL" ]; then
    report_publication=partial
else
    report_publication=absent
fi

if rm -f "$STAGED" && rmdir "$STAGE_DIR"; then
    cleanup=pass
else
    cleanup=fail
fi
finish_marker || :
exit "$collector_status"
