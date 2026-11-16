#!/bin/sh
set -eu

SDK_URL=https://github.com/ioma8/mpc-sample-sdk.git
SDK_COMMIT=e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39
PATCH_SET=m03b-a4-process-aware-v3

script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
experiment_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
repo_dir=$(CDPATH= cd -- "$experiment_dir/../.." && pwd)
destination=${1:-"$repo_dir/target/m03b-a/sdk"}
patch_dir="$experiment_dir/patches"

case $destination in
    /|/home|/tmp|'') echo "refusing unsafe destination: $destination" >&2; exit 2 ;;
esac

parent=$(dirname "$destination")
mkdir -p "$parent"
stage="$destination.prepare.$$"
trap 'rm -rf "$stage"' EXIT HUP INT TERM
rm -rf "$stage"

git clone --quiet --no-checkout "$SDK_URL" "$stage"
git -C "$stage" checkout --quiet --detach "$SDK_COMMIT"
actual=$(git -C "$stage" rev-parse HEAD)
[ "$actual" = "$SDK_COMMIT" ] || {
    echo "SDK revision mismatch: expected $SDK_COMMIT, got $actual" >&2
    exit 1
}

for patch in "$patch_dir"/*.patch; do
    git -C "$stage" apply --check "$patch"
    git -C "$stage" apply "$patch"
done

patch_hash=$(sha256sum "$patch_dir"/*.patch | sha256sum | cut -d' ' -f1)
printf '%s\n' \
    "upstream=$SDK_COMMIT" \
    "patch_set=$PATCH_SET" \
    "patch_series_sha256=$patch_hash" \
    > "$stage/.hereticc-patchset"

rm -rf "$destination"
mv "$stage" "$destination"
trap - EXIT HUP INT TERM

printf 'prepared %s\n' "$destination"
cat "$destination/.hereticc-patchset"
