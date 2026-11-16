#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
first=$(mktemp -d)
second=$(mktemp -d)
trap 'rm -rf "$first" "$second"' EXIT HUP INT TERM

"$script_dir/prepare-sdk.sh" "$first/sdk" >/dev/null
"$script_dir/prepare-sdk.sh" "$second/sdk" >/dev/null

first_diff=$(git -C "$first/sdk" diff --binary | sha256sum | cut -d' ' -f1)
second_diff=$(git -C "$second/sdk" diff --binary | sha256sum | cut -d' ' -f1)
[ "$first_diff" = "$second_diff" ] || {
    echo "patched trees differ" >&2
    exit 1
}
cmp "$first/sdk/.neurogenic-patchset" "$second/sdk/.neurogenic-patchset"
printf 'reproducible_patch_diff_sha256=%s\n' "$first_diff"
