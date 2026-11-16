#!/usr/bin/env python3
"""Generate and strictly inspect the fixed P3.2k RAM-staged package."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import stat
from pathlib import Path

AUTORUN_UUID = b"f7bed1c9-7e90-4731-9194-c996666b4331"
FIRMWARE_SHA256 = "44507c7a7d3029c411b2e3780ea286ca3e08b7e3ffd7b5047d52f73bf5299d1f"
COLLECTOR_SHA256 = "58c0caa61a543203e4d34568b1011339a06e6fc6ea06d52d191828dc6dadc49c"
MAX_COLLECTOR_BYTES = 1024 * 1024
EXPECTED = {
    "autorun/PACKAGE-MANIFEST.json",
    "autorun/SHA256SUMS",
    "autorun/autorun.check",
    "autorun/autorun.json",
    "autorun/autorun.sh",
    "autorun/p3_2f_collector",
}


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def metadata() -> bytes:
    value = {"value0": {"scripts": [{
        "path": "autorun.sh",
        "askQuestion": False,
        "message": "NEUROGENIC RAM-staged lifecycle diagnostic",
        "yesButtonText": "Start",
        "noButtonText": "Cancel",
    }], "files": []}}
    return (json.dumps(value, indent=2) + "\n").encode()


def _regular(path: Path, maximum: int) -> bytes:
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode) or stat.S_ISLNK(info.st_mode):
        raise ValueError(f"not a regular non-symlink file: {path}")
    if info.st_size > maximum:
        raise ValueError(f"file exceeds {maximum} bytes: {path}")
    return path.read_bytes()


def _write(path: Path, data: bytes, mode: int) -> None:
    path.write_bytes(data)
    path.chmod(mode)


def generate(repo: Path, collector: Path, output: Path, revision: str) -> None:
    repo = repo.resolve(strict=True)
    output = output.resolve(strict=False)
    allowed = repo / "target/b1-p3-2k"
    if output == allowed or allowed not in output.parents:
        raise ValueError("output must name a directory below target/b1-p3-2k")
    if output.exists():
        raise ValueError("output already exists")
    if not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise ValueError("revision must be 40 lowercase hexadecimal characters")
    collector_data = _regular(collector.resolve(strict=True), MAX_COLLECTOR_BYTES)
    if digest(collector_data) != COLLECTOR_SHA256:
        raise ValueError("collector SHA-256 mismatch")
    wrapper = _regular(repo / "device/m03b-b1-p3-2k/autorun.sh", 16 * 1024)
    meta = metadata()
    root = output / "autorun"
    root.mkdir(parents=True, mode=0o755)
    _write(root / "autorun.sh", wrapper, 0o755)
    _write(root / "autorun.json", meta, 0o644)
    _write(root / "autorun.check", digest(meta + wrapper + AUTORUN_UUID).encode(), 0o644)
    _write(root / "p3_2f_collector", collector_data, 0o755)
    payload = {}
    for name in ("autorun.check", "autorun.json", "autorun.sh", "p3_2f_collector"):
        data = (root / name).read_bytes()
        payload[f"autorun/{name}"] = {"bytes": len(data), "sha256": digest(data)}
    manifest = {
        "format": "neurogenic-b1-p3.2k-ram-stage-package-v1",
        "firmware": "1.3.0.12",
        "firmware_prefix_bytes": 4096,
        "firmware_prefix_sha256": FIRMWARE_SHA256,
        "collector_sha256": COLLECTOR_SHA256,
        "collector_report": "b1-p3-2c-report.txt",
        "marker": "b1-p3-2k-startup/marker.txt",
        "ram_stage": "/run/neurogenic-b1-p3-2k/p3_2f_collector",
        "source_revision": revision,
        "payload": payload,
        "safety": {
            "hook": False,
            "preload": False,
            "service_control": False,
            "network_listener": False,
            "persistent_device_write": False,
            "single_collector_invocation": True,
        },
    }
    manifest_data = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode()
    _write(root / "PACKAGE-MANIFEST.json", manifest_data, 0o644)
    targets = (*payload, "autorun/PACKAGE-MANIFEST.json")
    sums = "".join(
        f"{digest((output / name).read_bytes())}  {name}\n" for name in sorted(targets)
    )
    _write(root / "SHA256SUMS", sums.encode(), 0o644)
    os.chmod(output, 0o755)
    os.chmod(root, 0o755)


def inspect(repo: Path, package: Path) -> dict[str, object]:
    repo = repo.resolve(strict=True)
    package = package.resolve(strict=True)
    violations: list[str] = []
    found: set[str] = set()
    for path in package.rglob("*"):
        relative = path.relative_to(package).as_posix()
        if path.is_symlink():
            violations.append(f"symlink: {relative}")
        elif path.is_file():
            found.add(relative)
        elif not path.is_dir():
            violations.append(f"special entry: {relative}")
    if found != EXPECTED:
        violations.append("unexpected inventory")
    try:
        wrapper = (package / "autorun/autorun.sh").read_bytes()
        meta = (package / "autorun/autorun.json").read_bytes()
        check = (package / "autorun/autorun.check").read_text("ascii")
        collector = (package / "autorun/p3_2f_collector").read_bytes()
        manifest = json.loads((package / "autorun/PACKAGE-MANIFEST.json").read_text())
        sums = (package / "autorun/SHA256SUMS").read_text("ascii").splitlines()
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        return {"classification": "reject", "violations": [str(error)]}
    source = (repo / "device/m03b-b1-p3-2k/autorun.sh").read_bytes()
    if wrapper != source:
        violations.append("wrapper mismatch")
    if meta != metadata() or check != digest(meta + wrapper + AUTORUN_UUID):
        violations.append("recognition metadata")
    if digest(collector) != COLLECTOR_SHA256:
        violations.append("collector mismatch")
    text = wrapper.decode("utf-8", "replace")
    forbidden = (
        "systemctl", "systemd-run", "daemon-reload", "ac50.service.d",
        "software-update", "TcpListener", "ptrace", "process_vm_writev",
        "LD_PRELOAD=", "&\n", "\nwhile ", "\nfor ",
    )
    if any(token in text for token in forbidden):
        violations.append("prohibited operation")
    required = (
        'STAGE_DIR="/run/neurogenic-b1-p3-2k"',
        'STAGED="$STAGE_DIR/p3_2f_collector"',
        'mkdir "$STAGE_DIR"', 'cp "$COLLECTOR" "$STAGED"',
        'chmod 700 "$STAGED"', '"$STAGED"', 'collector_status=$?',
        'rm -f "$STAGED"', 'rmdir "$STAGE_DIR"',
        "schema=neurogenic-b1-ram-stage-v1", COLLECTOR_SHA256,
        FIRMWARE_SHA256,
    )
    if not all(token in text for token in required):
        violations.append("control-flow structure")
    if text.count('\n"$STAGED"\n') != 1 or 'exec "$STAGED"' in text:
        violations.append("collector invocation count")
    parsed: dict[str, str] = {}
    for line in sums:
        match = re.fullmatch(r"([0-9a-f]{64})  (autorun/[A-Za-z0-9_.-]+)", line)
        if not match or match.group(2) in parsed:
            violations.append("checksum syntax")
        else:
            parsed[match.group(2)] = match.group(1)
    targets = EXPECTED - {"autorun/SHA256SUMS"}
    if set(parsed) != targets or any(
        parsed.get(name) != digest((package / name).read_bytes()) for name in targets
    ):
        violations.append("checksum mismatch")
    if (
        manifest.get("firmware_prefix_sha256") != FIRMWARE_SHA256
        or manifest.get("collector_sha256") != COLLECTOR_SHA256
        or manifest.get("ram_stage") != "/run/neurogenic-b1-p3-2k/p3_2f_collector"
    ):
        violations.append("manifest")
    modes = {name: stat.S_IMODE((package / name).stat().st_mode) for name in EXPECTED}
    executable = {"autorun/autorun.sh", "autorun/p3_2f_collector"}
    if any(mode != (0o755 if name in executable else 0o644) for name, mode in modes.items()):
        violations.append("mode")
    package_hash = digest(b"".join((package / name).read_bytes() for name in sorted(EXPECTED)))
    return {
        "classification": "pass" if not violations else "reject",
        "package_sha256": package_hash,
        "violations": sorted(set(violations)),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True, type=Path)
    parser.add_argument("--collector", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--revision")
    parser.add_argument("--inspect", type=Path)
    args = parser.parse_args()
    if bool(args.output) == bool(args.inspect):
        parser.error("choose exactly one of --output or --inspect")
    try:
        if args.output:
            if not args.collector:
                parser.error("--collector is required with --output")
            generate(args.repo, args.collector, args.output, args.revision or "")
            return 0
        report = inspect(args.repo, args.inspect)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if report["classification"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
