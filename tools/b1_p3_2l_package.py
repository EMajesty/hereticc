#!/usr/bin/env python3
"""Generate and inspect the fixed P3.2l failure-stage package."""

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
COLLECTOR_SHA256 = "364db395b23823233b70024a66b2f4646ddfe8fc01ca07af451ae4cc8034e01c"
OLD_COLLECTOR_SHA256 = "58c0caa61a543203e4d34568b1011339a06e6fc6ea06d52d191828dc6dadc49c"
EXPECTED = {
    "autorun/PACKAGE-MANIFEST.json", "autorun/SHA256SUMS", "autorun/autorun.check",
    "autorun/autorun.json", "autorun/autorun.sh", "autorun/p3_2l_collector",
}
STAGES = (
    "entry-reached", "firmware-passed", "sd-validation-passed",
    "service-process-acquired", "acquisition-passed", "collection-passed",
    "report-publication-attempted", "report-publication-failed",
    "report-publication-passed",
)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def metadata() -> bytes:
    value = {"value0": {"scripts": [{
        "path": "autorun.sh", "askQuestion": False,
        "message": "NEUROGENIC collector-stage diagnostic",
        "yesButtonText": "Start", "noButtonText": "Cancel",
    }], "files": []}}
    return (json.dumps(value, indent=2) + "\n").encode()


def wrapper_source(repo: Path) -> bytes:
    text = (repo / "device/m03b-b1-p3-2k/autorun.sh").read_text()
    text = text.replace("B1-P3.2k", "B1-P3.2l").replace("p3-2k", "p3-2l")
    text = text.replace("p3_2f_collector", "p3_2l_collector")
    text = text.replace(OLD_COLLECTOR_SHA256, COLLECTOR_SHA256)
    text = text.replace("schema=neurogenic-b1-ram-stage-v1", "schema=neurogenic-b1-exit-stage-v1")
    text = text.replace(
        "cleanup=not-reached\nwrite_marker()",
        "cleanup=not-reached\ncollector_stage=not-observed\nwrite_marker()",
    )
    text = text.replace(
        '        "cleanup=$cleanup" > "$PARTIAL"',
        '        "cleanup=$cleanup" \\\n        "collector_stage=$collector_stage" > "$PARTIAL"',
    )
    text = text.replace(
        'STAGED="$STAGE_DIR/p3_2l_collector"',
        'STAGED="$STAGE_DIR/p3_2l_collector"\nSTAGE_STATUS="$STAGE_DIR/collector-stage.txt"',
    )
    evidence = '''collector_exit=$collector_status
if [ -f "$STAGE_STATUS" ] && [ ! -L "$STAGE_STATUS" ]; then
    IFS= read -r observed_stage < "$STAGE_STATUS" || observed_stage=invalid
    case "$observed_stage" in
        entry-reached|firmware-passed|sd-validation-passed|service-process-acquired|acquisition-passed|collection-passed|report-publication-attempted|report-publication-failed|report-publication-passed)
            collector_stage=$observed_stage ;;
        *) collector_stage=invalid ;;
    esac
fi'''
    text = text.replace("collector_exit=$collector_status", evidence)
    text = text.replace(
        'if rm -f "$STAGED" && rmdir "$STAGE_DIR"; then',
        'if rm -f "$STAGED" "$STAGE_STATUS" && rmdir "$STAGE_DIR"; then',
    )
    if "p3-2k" in text or OLD_COLLECTOR_SHA256 in text:
        raise ValueError("incomplete wrapper transformation")
    return text.encode()


def _write(path: Path, data: bytes, mode: int) -> None:
    path.write_bytes(data)
    path.chmod(mode)


def generate(repo: Path, collector: Path, output: Path, revision: str) -> None:
    repo, collector = repo.resolve(strict=True), collector.resolve(strict=True)
    output = output.resolve(strict=False)
    allowed = repo / "target/b1-p3-2l"
    if output == allowed or allowed not in output.parents or output.exists():
        raise ValueError("output must be a new directory below target/b1-p3-2l")
    if not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise ValueError("invalid revision")
    collector_data = collector.read_bytes()
    if digest(collector_data) != COLLECTOR_SHA256:
        raise ValueError("collector SHA-256 mismatch")
    wrapper, meta = wrapper_source(repo), metadata()
    root = output / "autorun"
    root.mkdir(parents=True, mode=0o755)
    for name, data, mode in (
        ("autorun.sh", wrapper, 0o755), ("autorun.json", meta, 0o644),
        ("autorun.check", digest(meta + wrapper + AUTORUN_UUID).encode(), 0o644),
        ("p3_2l_collector", collector_data, 0o755),
    ):
        _write(root / name, data, mode)
    payload = {}
    for name in ("autorun.check", "autorun.json", "autorun.sh", "p3_2l_collector"):
        data = (root / name).read_bytes()
        payload[f"autorun/{name}"] = {"bytes": len(data), "sha256": digest(data)}
    manifest = {
        "format": "neurogenic-b1-p3.2l-exit-stage-package-v1",
        "firmware": "1.3.0.12", "firmware_prefix_sha256": FIRMWARE_SHA256,
        "collector_sha256": COLLECTOR_SHA256, "source_revision": revision,
        "ram_stage": "/run/neurogenic-b1-p3-2l/p3_2l_collector",
        "stage_file": "/run/neurogenic-b1-p3-2l/collector-stage.txt",
        "marker": "b1-p3-2l-startup/marker.txt", "payload": payload,
        "safety": {"hook": False, "preload": False, "service_control": False,
                   "network_listener": False, "single_invocation": True},
    }
    manifest_data = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode()
    _write(root / "PACKAGE-MANIFEST.json", manifest_data, 0o644)
    targets = (*payload, "autorun/PACKAGE-MANIFEST.json")
    sums = "".join(f"{digest((output / n).read_bytes())}  {n}\n" for n in sorted(targets))
    _write(root / "SHA256SUMS", sums.encode(), 0o644)
    os.chmod(output, 0o755)
    os.chmod(root, 0o755)


def inspect(repo: Path, package: Path) -> dict[str, object]:
    repo, package = repo.resolve(strict=True), package.resolve(strict=True)
    violations = []
    entries = list(package.rglob("*"))
    found = {p.relative_to(package).as_posix() for p in entries if p.is_file() and not p.is_symlink()}
    if found != EXPECTED or any(p.is_symlink() for p in entries):
        violations.append("inventory")
    try:
        wrapper = (package / "autorun/autorun.sh").read_bytes()
        meta = (package / "autorun/autorun.json").read_bytes()
        check = (package / "autorun/autorun.check").read_text("ascii")
        collector = (package / "autorun/p3_2l_collector").read_bytes()
        manifest = json.loads((package / "autorun/PACKAGE-MANIFEST.json").read_text())
        sums = (package / "autorun/SHA256SUMS").read_text("ascii").splitlines()
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        return {"classification": "reject", "violations": [str(error)]}
    if wrapper != wrapper_source(repo) or meta != metadata() or check != digest(meta + wrapper + AUTORUN_UUID):
        violations.append("recognition or wrapper")
    if digest(collector) != COLLECTOR_SHA256:
        violations.append("collector")
    text = wrapper.decode("utf-8", "replace")
    if any(token in text for token in ("systemctl", "daemon-reload", "LD_PRELOAD=", "TcpListener", "&\n")):
        violations.append("prohibited operation")
    if text.count('\n"$STAGED"\n') != 1 or not all(stage in text for stage in STAGES):
        violations.append("stage structure")
    parsed = {}
    for line in sums:
        match = re.fullmatch(r"([0-9a-f]{64})  (autorun/[A-Za-z0-9_.-]+)", line)
        if not match or match.group(2) in parsed:
            violations.append("checksum syntax")
        else:
            parsed[match.group(2)] = match.group(1)
    targets = EXPECTED - {"autorun/SHA256SUMS"}
    if set(parsed) != targets or any(parsed.get(n) != digest((package / n).read_bytes()) for n in targets):
        violations.append("checksums")
    if manifest.get("collector_sha256") != COLLECTOR_SHA256:
        violations.append("manifest")
    executable = {"autorun/autorun.sh", "autorun/p3_2l_collector"}
    if any(stat.S_IMODE((package / n).stat().st_mode) != (0o755 if n in executable else 0o644) for n in EXPECTED):
        violations.append("mode")
    return {"classification": "pass" if not violations else "reject",
            "package_sha256": digest(b"".join((package / n).read_bytes() for n in sorted(EXPECTED))),
            "violations": sorted(set(violations))}


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
    if args.output:
        if not args.collector:
            parser.error("--collector is required")
        generate(args.repo, args.collector, args.output, args.revision or "")
        return 0
    result = inspect(args.repo, args.inspect)
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if result["classification"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
