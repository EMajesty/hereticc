#!/bin/sh
set -eu

SDK_URL=https://github.com/ioma8/mpc-sample-sdk.git
SDK_COMMIT=9487cbba3648afb29d8e3107e31a7ac930985220
PATCH_SET=m06d-read-only-transport-v1

script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
experiment_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
repo_dir=$(CDPATH= cd -- "$experiment_dir/../.." && pwd)
destination=${1:-"$repo_dir/target/m06d/sdk"}

case $destination in
    /|/home|/tmp|'') echo "refusing unsafe destination: $destination" >&2; exit 2 ;;
esac

mkdir -p "$(dirname "$destination")"
stage="$destination.prepare.$$"
trap 'rm -rf "$stage"' EXIT HUP INT TERM
rm -rf "$stage"
git clone --quiet --no-checkout "$SDK_URL" "$stage"
git -C "$stage" checkout --quiet --detach "$SDK_COMMIT"
[ "$(git -C "$stage" rev-parse HEAD)" = "$SDK_COMMIT" ] || exit 1

patch="$experiment_dir/patches/0001-add-read-only-transport-observer.patch"
git -C "$stage" apply --check "$patch"
git -C "$stage" apply "$patch"
patch_hash=$(sha256sum "$patch" | cut -d' ' -f1)
printf '%s\n' "upstream=$SDK_COMMIT" "patch_set=$PATCH_SET" \
    "patch_sha256=$patch_hash" > "$stage/.neurogenic-patchset"
rm -rf "$destination"
mv "$stage" "$destination"
trap - EXIT HUP INT TERM
cat "$destination/.neurogenic-patchset"
