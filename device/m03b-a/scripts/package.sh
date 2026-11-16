#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
experiment_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
repo_dir=$(CDPATH= cd -- "$experiment_dir/../.." && pwd)
sdk_dir="$repo_dir/target/m03b-a/sdk"
output_dir="$experiment_dir/build"
firmware_sha256=44507c7a7d3029c411b2e3780ea286ca3e08b7e3ffd7b5047d52f73bf5299d1f
export RUSTUP_HOME="$repo_dir/target/m03b-a/rustup"
export CARGO_HOME="$repo_dir/target/m03b-a/cargo-home"
rustup_bin=$(dirname "$(command -v rustup)")
PATH="$rustup_bin:$PATH"
export PATH RUSTUP_TOOLCHAIN=1.97.0

"$script_dir/inspect-elf.sh" "$output_dir/neurogenic-m03b-a-probe.elf"

"$sdk_dir/build-mod.sh" \
    --manifest-path "$experiment_dir/Cargo.toml" \
    --package neurogenic-m03b-a-probe \
    --name neurogenic-m03b-a-probe \
    --output-dir "$output_dir" \
    --pack \
    --firmware-sha256 "$firmware_sha256" \
    --display-name "NEUROGENIC M0.3b-A probe"

"$script_dir/inspect-elf.sh" "$output_dir/neurogenic-m03b-a-probe.elf"

rm -rf "$output_dir/inspect"
mkdir -p "$output_dir/inspect"
unzip -q "$output_dir/neurogenic-m03b-a-probe.zip" -d "$output_dir/inspect"
find "$output_dir/inspect" -type f -print | LC_ALL=C sort

hashes="$output_dir/SHA256SUMS"
(
    cd "$output_dir"
    sha256sum \
        neurogenic-m03b-a-probe.elf \
        neurogenic-m03b-a-probe.so \
        neurogenic-m03b-a-probe.zip \
        inspect/autorun/autorun.check \
        inspect/autorun/autorun.json \
        inspect/autorun/autorun.sh \
        inspect/autorun/neurogenic-m03b-a-probe.so
) > "$hashes"
cat "$hashes"
