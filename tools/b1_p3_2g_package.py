#!/usr/bin/env python3
"""Build the fixed, host-only P3.2g autorun directory for inspection."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import stat
from pathlib import Path

FIRMWARE = "1.3.0.12"
FIRMWARE_SHA256 = "44507c7a7d3029c411b2e3780ea286ca3e08b7e3ffd7b5047d52f73bf5299d1f"
COLLECTOR_SHA256 = "58c0caa61a543203e4d34568b1011339a06e6fc6ea06d52d191828dc6dadc49c"
AUTORUN_UUID = b"f7bed1c9-7e90-4731-9194-c996666b4331"
SCHEMA = "neurogenic-b1-p3.2c-v1"
MAX_COLLECTOR_BYTES = 1024 * 1024
BLOCKED_ROOTS = (Path("/media"), Path("/mnt"), Path("/run/media"), Path("/dev"))


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _regular(path: Path, maximum: int) -> bytes:
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode) or stat.S_ISLNK(info.st_mode):
        raise ValueError(f"not a regular non-symlink file: {path}")
    if info.st_size > maximum:
        raise ValueError(f"file exceeds {maximum} bytes: {path}")
    return path.read_bytes()


def validate_output(repo: Path, output: Path) -> Path:
    repo = repo.resolve(strict=True)
    candidate = output.resolve(strict=False)
    if any(candidate == root or root in candidate.parents for root in BLOCKED_ROOTS):
        raise ValueError("removable-device output paths are prohibited")
    expected = repo / "target" / "b1-p3-2g"
    if candidate != expected and expected not in candidate.parents:
        raise ValueError("output must be below target/b1-p3-2g")
    if candidate == expected:
        raise ValueError("output must name a package directory below target/b1-p3-2g")
    return candidate


def _metadata() -> bytes:
    value = {
        "value0": {
            "scripts": [{
                "path": "autorun.sh",
                "askQuestion": False,
                "message": "Start NEUROGENIC zero-hook lifecycle diagnostic?",
                "yesButtonText": "Start",
                "noButtonText": "Cancel",
            }],
            "files": [],
        }
    }
    return (json.dumps(value, indent=2) + "\n").encode("utf-8")


def _write(path: Path, data: bytes, mode: int) -> None:
    path.write_bytes(data)
    path.chmod(mode)


def generate(repo: Path, collector: Path, output: Path, source_revision: str) -> dict[str, object]:
    if not re.fullmatch(r"[0-9a-f]{40}", source_revision):
        raise ValueError("source revision must be 40 lowercase hexadecimal characters")
    repo = repo.resolve(strict=True)
    output = validate_output(repo, output)
    if output.exists():
        raise ValueError("output already exists")
    collector_data = _regular(collector.resolve(strict=True), MAX_COLLECTOR_BYTES)
    if digest(collector_data) != COLLECTOR_SHA256:
        raise ValueError("collector SHA-256 does not match the reviewed P3.2f executable")
    wrapper = _regular(repo / "device/m03b-b1-p3-2g/autorun.sh", 8192)
    metadata = _metadata()
    check = digest(metadata + wrapper + AUTORUN_UUID).encode("ascii")
    autorun = output / "autorun"
    autorun.mkdir(parents=True, mode=0o755)
    _write(autorun / "autorun.json", metadata, 0o644)
    _write(autorun / "autorun.sh", wrapper, 0o755)
    _write(autorun / "autorun.check", check, 0o644)
    _write(autorun / "p3_2f_collector", collector_data, 0o755)
    payload = {}
    for name in ("autorun.check", "autorun.json", "autorun.sh", "p3_2f_collector"):
        data = (autorun / name).read_bytes()
        payload[f"autorun/{name}"] = {"bytes": len(data), "sha256": digest(data)}
    manifest = {
        "format": "neurogenic-b1-p3.2g-package-v1",
        "firmware": FIRMWARE,
        "firmware_prefix_bytes": 4096,
        "firmware_prefix_sha256": FIRMWARE_SHA256,
        "collector_sha256": COLLECTOR_SHA256,
        "evidence_schema": SCHEMA,
        "source_revision": source_revision,
        "build": {
            "rust_toolchain": "1.97.0",
            "target": "aarch64-unknown-linux-gnu",
            "link_target": "aarch64-linux-gnu.2.17",
            "build_id": "disabled",
            "expected_interpreter": "/lib/ld-linux-aarch64.so.1",
        },
        "payload": payload,
        "safety": {
            "hook": False, "network_listener": False, "preload": False,
            "service_control": False, "persistent_device_write": False,
            "single_collector_invocation": True,
        },
    }
    manifest_data = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode()
    _write(autorun / "PACKAGE-MANIFEST.json", manifest_data, 0o644)
    sums = []
    for name in sorted((*payload, "autorun/PACKAGE-MANIFEST.json")):
        sums.append(f"{digest((output / name).read_bytes())}  {name}\n")
    _write(autorun / "SHA256SUMS", "".join(sums).encode("ascii"), 0o644)
    os.chmod(output, 0o755)
    os.chmod(autorun, 0o755)
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True, type=Path)
    parser.add_argument("--collector", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--source-revision", required=True)
    args = parser.parse_args()
    try:
        generate(args.repo, args.collector, args.output, args.source_revision)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
