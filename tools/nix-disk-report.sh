#!/usr/bin/env bash
set -euo pipefail

echo "NEUROGENIC Nix disk report (read-only)"
echo
df -h /nix/store
echo
echo "GC roots retaining store paths:"
nix-store --gc --print-roots 2>/dev/null || true
echo
echo "Project build links:"
find . -maxdepth 2 -type l -lname '/nix/store/*' -print -exec readlink {} \;
echo
echo "Current system closure:"
nix path-info -Sh /run/current-system 2>/dev/null || true
