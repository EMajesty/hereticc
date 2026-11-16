#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../../.." && pwd)
sdk_dir="$repo_dir/target/m03c/sdk"
output_dir="$repo_dir/target/m03d1/build"
firmware_sha256=44507c7a7d3029c411b2e3780ea286ca3e08b7e3ffd7b5047d52f73bf5299d1f

export RUSTUP_HOME="$repo_dir/target/m03b-a/rustup"
export CARGO_HOME="$repo_dir/target/m03b-a/cargo-home"
rustup_bin=$(dirname "$(command -v rustup)")
PATH="$rustup_bin:$PATH"
export PATH RUSTUP_TOOLCHAIN=1.97.0

"$script_dir/prepare-sdk.sh"
mkdir -p "$output_dir"
"$sdk_dir/build-mod.sh" \
    --manifest-path "$repo_dir/Cargo.toml" \
    --package neurogenic-device \
    --name neurogenic-m03d1-pad \
    --output-dir "$output_dir" \
    --pack \
    --firmware-sha256 "$firmware_sha256" \
    --display-name "NEUROGENIC M0.3d.1 Pad"

python3 "$script_dir/inspect-package.py" \
    "$output_dir/neurogenic-m03d1-pad.zip" \
    "$output_dir/neurogenic-m03d1-pad.so"
(
    cd "$output_dir"
    sha256sum neurogenic-m03d1-pad.elf neurogenic-m03d1-pad.so neurogenic-m03d1-pad.zip
) > "$output_dir/SHA256SUMS"
cat "$output_dir/SHA256SUMS"
