#!/usr/bin/env python3
"""Static structural safety inspection for the host-only P3.2d crate."""

from __future__ import annotations

import argparse
import json
import re
import tomllib
from pathlib import Path

MAX_SOURCE_BYTES = 256 * 1024
FORBIDDEN_CODE_PATTERNS = {
    "background/process spawning": r"\b(?:std::process|Command|spawn)\b",
    "network API/listener": r"\b(?:std::net|TcpListener|UdpSocket)\b",
    "process memory or unsafe code": r"\b(?:unsafe|ptrace|process_vm_writev|libc)\b",
    "filesystem traversal": r"\b(?:read_dir|WalkDir|glob)\b",
}
FORBIDDEN_TEXT = {
    "LD_PRELOAD",
    "systemctl restart",
    "systemctl daemon-reload",
    "systemd-run",
    "/software-update",
    "flash firmware",
}


def _without_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//.*", "", text)


def inspect(crate: Path) -> dict[str, object]:
    crate = crate.resolve(strict=True)
    manifest_path = crate / "Cargo.toml"
    source_path = crate / "src" / "lib.rs"
    manifest = tomllib.loads(manifest_path.read_text(encoding="utf-8"))
    data = source_path.read_bytes()
    if len(data) > MAX_SOURCE_BYTES:
        raise ValueError("collector source exceeds static-inspection bound")
    source = data.decode("utf-8")
    code = _without_comments(source)
    violations = []
    if manifest.get("dependencies") or manifest.get("dev-dependencies"):
        violations.append("external dependencies")
    if manifest.get("bin"):
        violations.append("binary target")
    bin_directory = crate / "src" / "bin"
    if (crate / "src" / "main.rs").exists() or (
        bin_directory.exists() and bool(list(bin_directory.glob("*.rs")))
    ):
        violations.append("executable source target")
    for description, pattern in FORBIDDEN_CODE_PATTERNS.items():
        if re.search(pattern, code):
            violations.append(description)
    for phrase in FORBIDDEN_TEXT:
        if phrase in source:
            violations.append(f"prohibited operation text: {phrase}")
    return {
        "classification": "pass" if not violations else "reject",
        "violations": sorted(set(violations)),
        "verified": {
            "library_only": "binary target" not in violations and "executable source target" not in violations,
            "no_external_dependencies": "external dependencies" not in violations,
            "no_process_or_network_api": not any(
                item in violations for item in ("background/process spawning", "network API/listener")
            ),
            "no_unsafe_or_memory_modification": "process memory or unsafe code" not in violations,
            "no_unrestricted_traversal": "filesystem traversal" not in violations,
        },
        "unverified": [
            "future device adapter command availability and timeout mechanism",
            "future invocation wrapper and AArch64 binary contents",
            "physical mount identity and runtime privileges",
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("crate", type=Path)
    arguments = parser.parse_args()
    report = inspect(arguments.crate)
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if report["classification"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
