#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../../.." && pwd)
source_dir="$repo_dir/crates/neurogenic-device/native"
output_dir="$repo_dir/target/m04a/abi"
cxx=${AARCH64_CXX:-aarch64-unknown-linux-gnu-g++}

mkdir -p "$output_dir"
"$cxx" -std=c++17 -O2 -fexceptions -fno-rtti -Wall -Wextra -Werror \
    "$source_dir/stock_sequence_fixture.cpp" "$source_dir/stock_sequence_abi.S" \
    -o "$output_dir/stock-sequence-fixture"
qemu-aarch64 "$output_dir/stock-sequence-fixture"
