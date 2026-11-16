#!/bin/sh
set -eu
script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
experiment_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
elf=${1:-"$experiment_dir/build/hereticc-m03b-b0-input-probe.elf"}
[ -f "$elf" ] || { echo "missing ELF: $elf" >&2; exit 1; }

header=$(llvm-readelf -h "$elf")
printf '%s\n' "$header" | grep -Fq 'Machine:                           AArch64'
printf '%s\n' "$header" | grep -Fq 'Type:                              DYN (Shared object file)'
symbols=$(llvm-readelf --dyn-syms "$elf")
if printf '%s\n' "$symbols" | grep -Eiq 'snd_|alsa|install_paint|send_note'; then
    echo "forbidden interposition or unrelated hook symbol found" >&2
    exit 1
fi
defined=$(printf '%s\n' "$symbols" | awk '$7 != "UND" && ($5 == "GLOBAL" || $5 == "WEAK") { print }')
[ -z "$defined" ] || { echo "unexpected dynamic exports" >&2; printf '%s\n' "$defined" >&2; exit 1; }
needed=$(llvm-readelf -d "$elf" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p')
for library in $needed; do
    case $library in libc.so.6|libpthread.so.0|libdl.so.2) ;; *) echo "unexpected dependency: $library" >&2; exit 1;; esac
done
sections=$(llvm-readelf -S "$elf")
printf '%s\n' "$sections" | grep -Fq '.init_array'
if printf '%s\n' "$sections" | awk '$0 ~ /PROGBITS|NOBITS/ && $0 ~ /W/ && $0 ~ /X/ { found=1 } END { exit !found }'; then
    echo "writable/executable ELF section found" >&2; exit 1
fi
binary_strings=$(strings -a "$elf")
printf '%s\n' "$binary_strings" | grep -F 'selected_hook=DATA_HOOK' >/dev/null
printf '%s\n' "$binary_strings" | grep -F '/run/hereticc-m03b-b0.txt' >/dev/null
printf '%s\n' "$binary_strings" | grep -F 'm03b-b01-' >/dev/null
printf '%s\n' "$binary_strings" | grep -F 'callback_invocations=' >/dev/null
if printf '%s\n' "$binary_strings" | grep -Ei 'SAMPLE_PAGE_VTABLE|NOTE_ON|NOTE_OFF|SURFACE|BUTTONS|install_paint|snd_|alsa' >/dev/null; then
    echo "unrelated hook/interposition identifier found" >&2; exit 1
fi

cat <<EOF
elf=$elf
machine=AArch64
type=ET_DYN
defined_dynamic_exports=0
alsa_interposition=absent
unrelated_hook_identifiers=absent
writable_executable_sections=absent
init_array=present
selected_hook=DATA_HOOK
initial_sd_reporting=present
bounded_atomic_counters=present
mutation_imports=expected-for-one-branch-hook
EOF
