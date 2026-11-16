#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../../.." && pwd)
sdk_dir="$repo_dir/target/m03c/sdk"
revision=9487cbba3648afb29d8e3107e31a7ac930985220
url=https://github.com/ioma8/mpc-sample-sdk.git

if [ ! -d "$sdk_dir/.git" ]; then
    git clone --filter=blob:none "$url" "$sdk_dir"
fi

[ "$(git -C "$sdk_dir" remote get-url origin)" = "$url" ] || {
    echo "unexpected SDK origin" >&2
    exit 1
}
git -C "$sdk_dir" fetch --quiet origin "$revision"
git -C "$sdk_dir" checkout --quiet --detach "$revision"
[ "$(git -C "$sdk_dir" rev-parse HEAD)" = "$revision" ] || exit 1
[ -z "$(git -C "$sdk_dir" status --short)" ] || {
    echo "SDK checkout is dirty" >&2
    exit 1
}
grep -q '^license = "MIT"$' "$sdk_dir/Cargo.toml"
echo "sdk_revision=$revision"
