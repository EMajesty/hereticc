#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../../.." && pwd)
sdk_dir="$repo_dir/target/m03c/sdk"
output_dir="$repo_dir/target/hil/build"

export RUSTUP_HOME="$repo_dir/target/m03b-a/rustup"
export CARGO_HOME="$repo_dir/target/m03b-a/cargo-home"
rustup_bin=$(dirname "$(command -v rustup)")
PATH="$rustup_bin:$PATH"
export PATH RUSTUP_TOOLCHAIN=1.97.0

"$repo_dir/device/m03e/scripts/prepare-sdk.sh"
mkdir -p "$output_dir"
"$sdk_dir/build-mod.sh" \
    --manifest-path "$repo_dir/Cargo.toml" \
    --package neurogenic-device-hil \
    --name neurogenic-hil-test \
    --output-dir "$output_dir"

(
    cd "$output_dir"
    sha256sum neurogenic-hil-test.elf neurogenic-hil-test.so
) > "$output_dir/SHA256SUMS"

python3 "$script_dir/inspect.py" "$output_dir/neurogenic-hil-test.so"
cat "$output_dir/SHA256SUMS"
