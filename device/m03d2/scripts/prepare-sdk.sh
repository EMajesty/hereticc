#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../../.." && pwd)

"$repo_dir/device/m03c/scripts/prepare-sdk.sh"
mkdir -p "$repo_dir/target/m03d2"
ln -sfn "$repo_dir/target/m03c/sdk" "$repo_dir/target/m03d2/sdk"
