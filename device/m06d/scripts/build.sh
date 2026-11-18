#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../../.." && pwd)
sdk_dir="$repo_dir/target/m06d/sdk"
output_dir="$repo_dir/target/m06d/build"
cargo_home="$repo_dir/target/m06d/cargo-home"

: "${AARCH64_CXX:?run inside the documented AArch64 GNU C++ environment}"
export RUSTUP_HOME="$repo_dir/target/m03b-a/rustup"
export CARGO_HOME="$cargo_home"
export CARGO_TARGET_DIR="$repo_dir/target/m06d/cargo"
rustup_bin=$(dirname "$(command -v rustup)")
PATH="$rustup_bin:$PATH"
export PATH RUSTUP_TOOLCHAIN=1.97.0

"$script_dir/prepare-sdk.sh"
mkdir -p "$cargo_home" "$output_dir"
printf '[patch."https://github.com/ioma8/mpc-sample-sdk"]\nmpc-sample-sdk = { path = "%s" }\n' \
    "$sdk_dir" > "$cargo_home/config.toml"
"$sdk_dir/build-mod.sh" \
    --manifest-path "$repo_dir/device/m06d/mod/Cargo.toml" \
    --package neurogenic-m06d-transport-hil \
    --name neurogenic-m06d-transport-observer \
    --output-dir "$output_dir"

python3 "$repo_dir/device/hil/scripts/inspect.py" \
    "$output_dir/neurogenic-m06d-transport-observer.so"
(
    cd "$output_dir"
    sha256sum neurogenic-m06d-transport-observer.elf neurogenic-m06d-transport-observer.so
) > "$output_dir/SHA256SUMS"
cat "$output_dir/SHA256SUMS"
