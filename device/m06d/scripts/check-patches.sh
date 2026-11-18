#!/bin/sh
set -eu
script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../../.." && pwd)
first="$repo_dir/target/m06d/repro-1"
second="$repo_dir/target/m06d/repro-2"
"$script_dir/prepare-sdk.sh" "$first"
"$script_dir/prepare-sdk.sh" "$second"
diff -ru --exclude=.git "$first" "$second"
printf 'M0.6d SDK patch application is reproducible\n'
