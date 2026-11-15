#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
experiment_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
repo_dir=$(CDPATH= cd -- "$experiment_dir/../.." && pwd)
sdk_dir="$repo_dir/target/m03b-a/sdk"
output_dir="$experiment_dir/build"
toolchain=1.97.0
export RUSTUP_HOME="$repo_dir/target/m03b-a/rustup"
export CARGO_HOME="$repo_dir/target/m03b-a/cargo-home"

rustup toolchain install "$toolchain" \
    --profile minimal \
    --target aarch64-unknown-linux-gnu
rustup component add --toolchain "$toolchain" rustfmt clippy
rustup_bin=$(dirname "$(command -v rustup)")
PATH="$rustup_bin:$PATH"
export PATH RUSTUP_TOOLCHAIN="$toolchain"

"$script_dir/prepare-sdk.sh" "$sdk_dir"
cargo fmt --manifest-path "$sdk_dir/Cargo.toml" -- --check
cargo clippy --manifest-path "$sdk_dir/Cargo.toml" --features probe-only --lib --tests -- -D warnings
cargo test --manifest-path "$sdk_dir/Cargo.toml" --features probe-only --lib
cargo fmt --manifest-path "$experiment_dir/Cargo.toml" -- --check
cargo clippy --manifest-path "$experiment_dir/Cargo.toml" --all-targets -- -D warnings
cargo test --manifest-path "$experiment_dir/Cargo.toml"

"$sdk_dir/build-mod.sh" \
    --manifest-path "$experiment_dir/Cargo.toml" \
    --package hereticc-m03b-a-probe \
    --name hereticc-m03b-a-probe \
    --output-dir "$output_dir"

printf 'built artifacts in %s\n' "$output_dir"
