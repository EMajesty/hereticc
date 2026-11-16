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
    dependencies = manifest.get("dependencies", {})
    allowed_dependencies = {
        name
        for name, value in dependencies.items()
        if name.startswith("neurogenic-") and isinstance(value, dict) and set(value) == {"path"}
    }
    if set(dependencies) != allowed_dependencies or manifest.get("dev-dependencies"):
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
    uses_process = bool(re.search(r"\b(?:std::process|Command|spawn)\b", code))
    if uses_process:
        fixed_runner = all(
            marker in code
            for marker in (
                "struct FixedCommand",
                ".env_clear()",
                ".stdin(Stdio::null())",
                ".process_group(0)",
                "child.kill()",
                "child.wait()",
            )
        )
        public_command_fields = bool(
            re.search(r"struct FixedCommand\s*\{[^}]*\bpub\s+(?:program|args|label)\s*:", code, re.DOTALL)
        )
        shell_tokens = bool(re.search(r'"(?:/bin/)?(?:sh|bash)"|"-c"', code))
        if not fixed_runner or public_command_fields or shell_tokens:
            violations.append("unbounded or arbitrary process execution")
    for phrase in FORBIDDEN_TEXT:
        if phrase in source:
            violations.append(f"prohibited operation text: {phrase}")
    return {
        "classification": "pass" if not violations else "reject",
        "violations": sorted(set(violations)),
        "verified": {
            "library_only": "binary target" not in violations and "executable source target" not in violations,
            "no_external_dependencies": "external dependencies" not in violations,
            "fixed_process_boundary_and_no_network_api": not any(
                item in violations
                for item in ("unbounded or arbitrary process execution", "network API/listener")
            ),
            "no_unsafe_or_memory_modification": "process memory or unsafe code" not in violations,
            "no_unrestricted_traversal": "filesystem traversal" not in violations,
        },
        "unverified": [
            "device command availability and complete descendant/timeout behavior",
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
