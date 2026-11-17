#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../../.." && pwd)
sdk_dir="$repo_dir/target/m03c/sdk"
output_dir="$repo_dir/target/m05a/build"

: "${AARCH64_CXX:?run inside the documented AArch64 GNU C++ environment}"
export RUSTUP_HOME="$repo_dir/target/m03b-a/rustup"
export CARGO_HOME="$repo_dir/target/m03b-a/cargo-home"
export CARGO_TARGET_DIR="$repo_dir/target/m05a/cargo"
rustup_bin=$(dirname "$(command -v rustup)")
PATH="$rustup_bin:$PATH"
export PATH RUSTUP_TOOLCHAIN=1.97.0

"$repo_dir/device/m03e/scripts/prepare-sdk.sh"
mkdir -p "$output_dir"
"$sdk_dir/build-mod.sh" \
    --manifest-path "$repo_dir/Cargo.toml" \
    --package neurogenic-device-native-grid \
    --name neurogenic-m05a-native-grid \
    --output-dir "$output_dir"

llvm-nm -D "$output_dir/neurogenic-m05a-native-grid.elf" | \
    grep -Fq ' neurogenic_m04a_native_adapter_link_anchor'
python3 "$repo_dir/device/hil/scripts/inspect.py" \
    "$output_dir/neurogenic-m05a-native-grid.so"
(
    cd "$output_dir"
    sha256sum neurogenic-m05a-native-grid.elf neurogenic-m05a-native-grid.so
) > "$output_dir/SHA256SUMS"
cat "$output_dir/SHA256SUMS"
