#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
experiment_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
elf=${1:-"$experiment_dir/build/hereticc-m03b-a-probe.elf"}

[ -f "$elf" ] || { echo "missing ELF: $elf" >&2; exit 1; }

header=$(llvm-readelf -h "$elf")
printf '%s\n' "$header" | grep -Fq 'Machine:                           AArch64'
printf '%s\n' "$header" | grep -Fq 'Type:                              DYN (Shared object file)'

symbols=$(llvm-readelf --dyn-syms "$elf")
if printf '%s\n' "$symbols" | grep -Eiq 'snd_|alsa|mprotect|pwrite|install_paint|send_note|hook'; then
    echo "forbidden interposition or mutation symbol found" >&2
    printf '%s\n' "$symbols" >&2
    exit 1
fi

defined=$(printf '%s\n' "$symbols" | awk '$7 != "UND" && ($5 == "GLOBAL" || $5 == "WEAK") { print }')
[ -z "$defined" ] || {
    echo "unexpected exported dynamic symbols:" >&2
    printf '%s\n' "$defined" >&2
    exit 1
}

needed=$(llvm-readelf -d "$elf" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p')
for library in $needed; do
    case $library in
        libc.so.6|libpthread.so.0|libdl.so.2) ;;
        *) echo "unexpected dynamic dependency: $library" >&2; exit 1 ;;
    esac
done

sections=$(llvm-readelf -S "$elf")
printf '%s\n' "$sections" | grep -Fq '.init_array'
if printf '%s\n' "$sections" | awk '$0 ~ /PROGBITS|NOBITS/ && $0 ~ /W/ && $0 ~ /X/ { found = 1 } END { exit !found }'; then
    echo "writable/executable ELF section found" >&2
    exit 1
fi

binary_strings=$(strings -a "$elf")
# Do not use grep -q for the large string table: with pipefail-enabled parent
# shells, grep's early exit can make the producer's SIGPIPE look like failure.
printf '%s\n' "$binary_strings" | grep -F '/run/hereticc-m03b-a.txt' >/dev/null
printf '%s\n' "$binary_strings" | grep -F '/proc/mounts' >/dev/null
printf '%s\n' "$binary_strings" | grep -F 'hereticc-results' >/dev/null
printf '%s\n' "$binary_strings" | grep -F 'm03b-a4-' >/dev/null
printf '%s\n' "$binary_strings" | grep -F 'report_category=' >/dev/null
if printf '%s\n' "$binary_strings" | grep -Ei 'snd_|alsa|install_paint|mpc_sample_sdk.*hook|sites::install' >/dev/null; then
    echo "forbidden feature-hook or interposition identifier found" >&2
    exit 1
fi

cat <<EOF
elf=$elf
machine=AArch64
type=ET_DYN
defined_dynamic_exports=0
needed=$(printf '%s' "$needed" | tr '\n' ',')
forbidden_symbols=absent
mprotect_import=absent
pwrite_import=absent
alsa_interposition=absent
writable_executable_sections=absent
init_array=present
report_path=present
direct_sd_policy=present
process_namespaces=present
EOF
