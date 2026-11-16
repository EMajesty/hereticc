#!/usr/bin/env python3
"""Generate an inert, deterministic P3.2d source-review manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
import stat
from pathlib import Path

MAX_FILE_BYTES = 1024 * 1024
SCHEMA = "neurogenic-b1-p3.2c-v1"
FIRMWARE = "1.3.0.12"
STOCK_PREFIX_SHA256 = "44507c7a7d3029c411b2e3780ea286ca3e08b7e3ffd7b5047d52f73bf5299d1f"
PROHIBITED_NAMES = {"autorun.sh", "autorun.json", "software-update"}
PROHIBITED_SUFFIXES = {".so", ".img", ".bin", ".elf", ".zip"}


def inventory(root: Path, paths: list[Path]) -> list[dict[str, object]]:
    root = root.resolve(strict=True)
    entries = []
    for path in paths:
        resolved = path.resolve(strict=True)
        try:
            relative = resolved.relative_to(root)
        except ValueError as error:
            raise ValueError("inventory file is outside the declared root") from error
        metadata = resolved.stat()
        if not stat.S_ISREG(metadata.st_mode):
            raise ValueError("inventory entry is not a regular file")
        if stat.S_IMODE(metadata.st_mode) & 0o111:
            raise ValueError("executable inventory entries are prohibited")
        if resolved.name in PROHIBITED_NAMES or resolved.suffix.lower() in PROHIBITED_SUFFIXES:
            raise ValueError("device-recognized or deployable artifact is prohibited")
        data = resolved.read_bytes()
        if len(data) > MAX_FILE_BYTES:
            raise ValueError(f"inventory file exceeds {MAX_FILE_BYTES} bytes")
        entries.append(
            {
                "path": relative.as_posix(),
                "bytes": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
            }
        )
    entries.sort(key=lambda item: item["path"])
    if len({entry["path"] for entry in entries}) != len(entries):
        raise ValueError("duplicate inventory entry")
    return entries


def build_manifest(root: Path, paths: list[Path]) -> dict[str, object]:
    return {
        "format": "neurogenic-b1-p3.2d-inert-manifest-v1",
        "firmware": FIRMWARE,
        "stock_executable_prefix_sha256": STOCK_PREFIX_SHA256,
        "evidence_schema": SCHEMA,
        "provenance": {
            "component": "neurogenic-b1-collector-model",
            "language": "Rust",
            "target_status": "host-tested-library-only",
        },
        "inventory": inventory(root, paths),
        "safety": {
            "autorun_entry_point": False,
            "background_process": False,
            "device_persistence": False,
            "firmware_update": False,
            "hook_or_preload": False,
            "network_listener": False,
            "process_memory_access": False,
            "service_install_or_control": False,
        },
        "unverified": [
            "device-side observation adapter availability",
            "future AArch64 invocation and deadline wrapper",
            "physical SD mount identity and permissions",
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--file", type=Path, action="append", default=[], required=True)
    arguments = parser.parse_args()
    print(json.dumps(build_manifest(arguments.root, arguments.file), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
