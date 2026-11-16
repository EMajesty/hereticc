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
    "process memory API": r"\b(?:ptrace|process_vm_writev)\b",
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


def _production_code(text: str) -> str:
    return _without_comments(text.split("#[cfg(test)]", 1)[0])


def inspect(crate: Path) -> dict[str, object]:
    crate = crate.resolve(strict=True)
    manifest_path = crate / "Cargo.toml"
    manifest = tomllib.loads(manifest_path.read_text(encoding="utf-8"))
    source_paths = sorted((crate / "src").rglob("*.rs"))
    source_data = {path: path.read_bytes() for path in source_paths}
    if sum(map(len, source_data.values())) > MAX_SOURCE_BYTES:
        raise ValueError("collector source exceeds static-inspection bound")
    sources = {path: data.decode("utf-8") for path, data in source_data.items()}
    source = "\n".join(sources.values())
    code = "\n".join(_production_code(text) for text in sources.values())
    violations = []
    dependencies = manifest.get("dependencies", {})
    allowed_dependencies = {
        name
        for name, value in dependencies.items()
        if (name.startswith("neurogenic-") and isinstance(value, dict) and set(value) == {"path"})
        or (crate.name == "neurogenic-b1-collector-linux" and name == "libc" and value == "0.2")
    }
    if set(dependencies) != allowed_dependencies or manifest.get("dev-dependencies"):
        violations.append("external dependencies")
    permitted_binary = crate.name == "neurogenic-b1-collector-linux" and (
        crate / "src" / "bin" / "p3_2f_collector.rs"
    ).is_file()
    if manifest.get("bin") and not permitted_binary:
        violations.append("binary target")
    bin_directory = crate / "src" / "bin"
    binaries = list(bin_directory.glob("*.rs")) if bin_directory.exists() else []
    if (crate / "src" / "main.rs").exists() or any(
        path.name != "p3_2f_collector.rs" or not permitted_binary for path in binaries
    ):
        violations.append("executable source target")
    if permitted_binary:
        binary = _production_code(sources[crate / "src" / "bin" / "p3_2f_collector.rs"])
        if "std::env::args" in binary or "clap" in binary or "verify_stock_prefix" not in binary:
            violations.append("generic or unguarded integration executable")
    for description, pattern in FORBIDDEN_CODE_PATTERNS.items():
        if re.search(pattern, code):
            violations.append(description)
    unsafe_paths = [
        path
        for path, text in sources.items()
        if re.search(r"\b(?:unsafe|libc)\b", _production_code(text))
    ]
    if unsafe_paths and (
        len(unsafe_paths) != 1
        or unsafe_paths[0] != crate / "src" / "sd.rs"
        or "#![allow(unsafe_code)]" not in sources[unsafe_paths[0]]
    ):
        violations.append("unsafe code outside isolated SD/containment boundary")
    uses_process = bool(re.search(r"\b(?:std::process|Command|spawn)\b", code))
    if uses_process:
        fixed_runner = all(
            marker in code
            for marker in (
                "struct FixedCommand",
                ".env_clear()",
                ".stdin(Stdio::null())",
                ".process_group(0)",
                "kill_process_group",
                ".wait()",
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
            "fixed_integration_only": "binary target" not in violations and "executable source target" not in violations,
            "reviewed_dependencies_only": "external dependencies" not in violations,
            "fixed_process_boundary_and_no_network_api": not any(
                item in violations
                for item in ("unbounded or arbitrary process execution", "network API/listener")
            ),
            "isolated_ffi_and_no_memory_modification": not any(
                item in violations
                for item in ("process memory API", "unsafe code outside isolated SD/containment boundary")
            ),
            "no_unrestricted_traversal": "filesystem traversal" not in violations,
        },
        "unverified": [
            "device command availability and complete descendant/timeout behavior",
            "future autorun invocation wrapper and physical AArch64 ABI compatibility",
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
