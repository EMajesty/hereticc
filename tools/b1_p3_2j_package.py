#!/usr/bin/env python3
"""Generate and inspect the fixed P3.2j marker-only autorun package."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import stat
from pathlib import Path

SALT = b"f7bed1c9-7e90-4731-9194-c996666b4331"
FW_HASH = "44507c7a7d3029c411b2e3780ea286ca3e08b7e3ffd7b5047d52f73bf5299d1f"
EXPECTED = {
    "autorun/PACKAGE-MANIFEST.json", "autorun/SHA256SUMS",
    "autorun/autorun.check", "autorun/autorun.json", "autorun/autorun.sh",
}


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def metadata() -> bytes:
    value = {"value0": {"scripts": [{
        "path": "autorun.sh", "askQuestion": False,
        "message": "NEUROGENIC startup marker",
        "yesButtonText": "Start", "noButtonText": "Cancel",
    }], "files": []}}
    return (json.dumps(value, indent=2) + "\n").encode()


def _write(path: Path, data: bytes, mode: int) -> None:
    path.write_bytes(data)
    path.chmod(mode)


def generate(repo: Path, output: Path, revision: str) -> None:
    repo = repo.resolve(strict=True)
    output = output.resolve(strict=False)
    allowed = repo / "target/b1-p3-2j"
    if output == allowed or allowed not in output.parents:
        raise ValueError("output must name a directory below target/b1-p3-2j")
    if output.exists():
        raise ValueError("output already exists")
    if not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise ValueError("revision must be 40 lowercase hexadecimal characters")
    wrapper = (repo / "device/m03b-b1-p3-2j/autorun.sh").read_bytes()
    meta = metadata()
    root = output / "autorun"
    root.mkdir(parents=True, mode=0o755)
    _write(root / "autorun.sh", wrapper, 0o755)
    _write(root / "autorun.json", meta, 0o644)
    _write(root / "autorun.check", sha(meta + wrapper + SALT).encode(), 0o644)
    payload = {}
    for name in ("autorun.check", "autorun.json", "autorun.sh"):
        data = (root / name).read_bytes()
        payload[f"autorun/{name}"] = {"bytes": len(data), "sha256": sha(data)}
    manifest = {
        "format": "neurogenic-b1-p3.2j-marker-package-v1",
        "firmware": "1.3.0.12", "firmware_prefix_sha256": FW_HASH,
        "marker": "b1-p3-2j-startup/marker.txt",
        "partial": "b1-p3-2j-startup/marker.partial",
        "source_revision": revision, "payload": payload,
        "safety": {"collector": False, "hook": False, "internal_write": False,
                   "network": False, "service_control": False},
    }
    manifest_data = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode()
    _write(root / "PACKAGE-MANIFEST.json", manifest_data, 0o644)
    targets = (*payload, "autorun/PACKAGE-MANIFEST.json")
    sums = "".join(f"{sha((output / name).read_bytes())}  {name}\n" for name in sorted(targets))
    _write(root / "SHA256SUMS", sums.encode(), 0o644)


def inspect(repo: Path, package: Path) -> dict[str, object]:
    repo, package = repo.resolve(strict=True), package.resolve(strict=True)
    violations = []
    found = {p.relative_to(package).as_posix() for p in package.rglob("*") if p.is_file()}
    if found != EXPECTED or any(p.is_symlink() for p in package.rglob("*")):
        violations.append("inventory or symlink")
    try:
        wrapper = (package / "autorun/autorun.sh").read_bytes()
        meta = (package / "autorun/autorun.json").read_bytes()
        check = (package / "autorun/autorun.check").read_text("ascii")
        manifest = json.loads((package / "autorun/PACKAGE-MANIFEST.json").read_text())
        sums = (package / "autorun/SHA256SUMS").read_text("ascii").splitlines()
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        return {"classification": "reject", "violations": [str(error)]}
    if wrapper != (repo / "device/m03b-b1-p3-2j/autorun.sh").read_bytes():
        violations.append("wrapper mismatch")
    if meta != metadata() or check != sha(meta + wrapper + SALT):
        violations.append("recognition metadata")
    text = wrapper.decode("utf-8", "replace")
    prohibited = ("LD_PRELOAD=", "systemctl", "daemon-reload", "restart ac50",
                  "TcpListener", "software-update", "p3_2f_collector", "&\n",
                  "\nwhile ", "\nfor ", "\nrm ", "\ncp ", "\nchmod ", "/run/")
    if any(token in text for token in prohibited):
        violations.append("prohibited operation")
    required = ("b1-p3-2j-startup", "marker.partial", "marker.txt",
                "autorun_entered=true", "collector_invocation=deferred",
                FW_HASH, 'mkdir "$MARKER_DIR"')
    if not all(token in text for token in required):
        violations.append("marker structure")
    if text.count('> "$PARTIAL"') != 1 or "mkdir \"$MARKER_DIR\" || exit 1" not in text:
        violations.append("publication boundary")
    parsed = {}
    for line in sums:
        match = re.fullmatch(r"([0-9a-f]{64})  (autorun/[A-Za-z0-9_.-]+)", line)
        if not match or match.group(2) in parsed:
            violations.append("checksum syntax")
        else:
            parsed[match.group(2)] = match.group(1)
    targets = EXPECTED - {"autorun/SHA256SUMS"}
    if set(parsed) != targets or any(parsed.get(name) != sha((package / name).read_bytes()) for name in targets):
        violations.append("checksum mismatch")
    if manifest.get("firmware_prefix_sha256") != FW_HASH or manifest.get("marker") != "b1-p3-2j-startup/marker.txt":
        violations.append("manifest")
    modes = {name: stat.S_IMODE((package / name).stat().st_mode) for name in EXPECTED}
    if any(mode != (0o755 if name == "autorun/autorun.sh" else 0o644) for name, mode in modes.items()):
        violations.append("mode")
    return {"classification": "pass" if not violations else "reject",
            "package_sha256": sha(b"".join((package / n).read_bytes() for n in sorted(EXPECTED))),
            "violations": sorted(set(violations))}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True, type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--revision")
    parser.add_argument("--inspect", type=Path)
    args = parser.parse_args()
    if bool(args.output) == bool(args.inspect):
        parser.error("choose exactly one of --output or --inspect")
    if args.output:
        generate(args.repo, args.output, args.revision or "")
        return 0
    report = inspect(args.repo, args.inspect)
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if report["classification"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
