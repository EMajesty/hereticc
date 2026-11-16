#!/usr/bin/env python3
"""Strict static inspection of a generated P3.2g package."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import stat
from pathlib import Path

from b1_p3_2g_package import (
    AUTORUN_UUID, COLLECTOR_SHA256, FIRMWARE_SHA256, _metadata,
)

EXPECTED = {
    "autorun/PACKAGE-MANIFEST.json", "autorun/SHA256SUMS", "autorun/autorun.check",
    "autorun/autorun.json", "autorun/autorun.sh", "autorun/p3_2f_collector",
}
FORBIDDEN = (
    "systemctl", "systemd-run", "daemon-reload", "restart ac50",
    "/software-update", "TcpListener", "ptrace", "process_vm_writev", "&\n",
)


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def inspect(repo: Path, package: Path) -> dict[str, object]:
    violations = []
    repo = repo.resolve(strict=True)
    package = package.resolve(strict=True)
    found = set()
    for path in package.rglob("*"):
        relative = path.relative_to(package).as_posix()
        if path.is_symlink():
            violations.append(f"symlink: {relative}")
        elif path.is_file():
            found.add(relative)
        elif not path.is_dir():
            violations.append(f"special entry: {relative}")
    if found != EXPECTED:
        violations.append("unexpected file inventory")
    try:
        wrapper = (package / "autorun/autorun.sh").read_bytes()
        source_wrapper = (repo / "device/m03b-b1-p3-2g/autorun.sh").read_bytes()
        metadata = (package / "autorun/autorun.json").read_bytes()
        check = (package / "autorun/autorun.check").read_text(encoding="ascii")
        collector = (package / "autorun/p3_2f_collector").read_bytes()
        manifest_data = (package / "autorun/PACKAGE-MANIFEST.json").read_bytes()
        manifest = json.loads(manifest_data)
        sums = (package / "autorun/SHA256SUMS").read_text(encoding="ascii").splitlines()
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        return {"classification": "reject", "violations": sorted(set(violations + [f"unreadable package: {error}"]))}
    if wrapper != source_wrapper:
        violations.append("wrapper differs from reviewed source")
    if metadata != _metadata():
        violations.append("autorun metadata mismatch")
    if check != sha(metadata + wrapper + AUTORUN_UUID):
        violations.append("autorun check mismatch")
    if sha(collector) != COLLECTOR_SHA256:
        violations.append("collector hash mismatch")
    if manifest.get("firmware_prefix_sha256") != FIRMWARE_SHA256:
        violations.append("firmware hash mismatch")
    if manifest.get("collector_sha256") != COLLECTOR_SHA256:
        violations.append("manifest collector hash mismatch")
    expected_payload = {name: {"bytes": len((package / name).read_bytes()), "sha256": sha((package / name).read_bytes())}
                        for name in sorted(EXPECTED - {"autorun/PACKAGE-MANIFEST.json", "autorun/SHA256SUMS"})}
    if manifest.get("payload") != expected_payload:
        violations.append("manifest payload mismatch")
    parsed_sums = {}
    for line in sums:
        match = re.fullmatch(r"([0-9a-f]{64})  (autorun/[A-Za-z0-9_.-]+)", line)
        if not match or match.group(2) in parsed_sums:
            violations.append("malformed checksum inventory")
            continue
        parsed_sums[match.group(2)] = match.group(1)
    sum_targets = EXPECTED - {"autorun/SHA256SUMS"}
    if set(parsed_sums) != sum_targets or any(parsed_sums.get(name) != sha((package / name).read_bytes()) for name in sum_targets):
        violations.append("checksum inventory mismatch")
    text = wrapper.decode("utf-8", errors="replace")
    if text.count("LD_PRELOAD") != 1 or "unset CDPATH ENV BASH_ENV LD_PRELOAD LD_LIBRARY_PATH" not in text:
        violations.append("preload environment is not solely cleared")
    for token in FORBIDDEN:
        if token in text:
            violations.append(f"prohibited wrapper operation: {token}")
    required = (
        '[ "$#" -eq 0 ]', 'STOCK="/usr/bin/MPC Sample"', f'FW="{FIRMWARE_SHA256}"',
        'exec "$COLLECTOR"', '[ ! -L "$COLLECTOR" ]',
    )
    if not all(token in text for token in required) or text.count('\nexec "$COLLECTOR"\n') != 1:
        violations.append("wrapper control-flow structure mismatch")
    modes = {name: stat.S_IMODE((package / name).stat().st_mode) for name in EXPECTED}
    expected_modes = {name: (0o755 if name in {"autorun/autorun.sh", "autorun/p3_2f_collector"} else 0o644) for name in EXPECTED}
    if modes != expected_modes:
        violations.append("file mode mismatch")
    return {
        "classification": "pass" if not violations else "reject",
        "package_sha256": sha(b"".join((package / name).read_bytes() for name in sorted(EXPECTED))),
        "violations": sorted(set(violations)),
        "verified": {
            "exact_inventory": found == EXPECTED,
            "a4_recognition_format": metadata == _metadata() and check == sha(metadata + wrapper + AUTORUN_UUID),
            "reviewed_wrapper": wrapper == source_wrapper,
            "reviewed_collector": sha(collector) == COLLECTOR_SHA256,
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True, type=Path)
    parser.add_argument("package", type=Path)
    args = parser.parse_args()
    report = inspect(args.repo, args.package)
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if report["classification"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
