#!/usr/bin/env python3
"""Offline, bounded inspection of redacted MPC Sample lifecycle evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path
from typing import Callable

MAX_INPUT_BYTES = 256 * 1024
EXPECTED_ADDRESS = "192.168.155.1"
EXPECTED_SUBNET = "192.168.155.0/28"
CATEGORIES = (
    "systemd",
    "process",
    "cgroup",
    "mount",
    "network",
    "sockets",
    "firewall",
    "stock",
)


def read_text(path: Path) -> tuple[str, dict[str, object]]:
    data = path.read_bytes()
    if len(data) > MAX_INPUT_BYTES:
        raise ValueError(f"input exceeds {MAX_INPUT_BYTES} bytes")
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as error:
        raise ValueError("input is not valid UTF-8") from error
    return text, {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def _fact(classification: str, value: object, evidence: str) -> dict[str, object]:
    return {"classification": classification, "value": value, "evidence": evidence}


def _single(values: set[object], evidence: str) -> dict[str, object]:
    if not values:
        return _fact("unknown", None, f"missing {evidence}")
    if len(values) > 1:
        return _fact("conflicting", None, f"multiple {evidence} observations")
    return _fact("confirmed", next(iter(values)), evidence)


def _properties(text: str) -> tuple[dict[str, set[str]], list[str], int]:
    expected = {
        "Id",
        "LoadState",
        "ActiveState",
        "SubState",
        "Type",
        "ExecStart",
        "User",
        "Group",
        "KillMode",
        "Restart",
        "RestartUSec",
        "TimeoutStopUSec",
        "Slice",
        "MainPID",
        "ControlGroup",
        "FragmentPath",
        "DropInPaths",
        "Requires",
        "Wants",
        "After",
        "Before",
    }
    found: dict[str, set[str]] = {}
    unexpected: set[str] = set()
    malformed = 0
    for line in text.splitlines():
        if not line.strip() or re.fullmatch(r"systemd \d+(?:\.\d+)?(?:\s+.*)?", line.strip()):
            continue
        if "=" not in line:
            malformed += 1
            continue
        key, value = line.split("=", 1)
        if not re.fullmatch(r"[A-Za-z][A-Za-z0-9]*", key):
            malformed += 1
            continue
        if key in expected:
            found.setdefault(key, set()).add(value.strip())
        else:
            unexpected.add(key)
    return found, sorted(unexpected), malformed


def analyze_systemd(text: str) -> dict[str, object]:
    properties, unexpected, malformed = _properties(text)
    versions = {
        int(value)
        for value in re.findall(r"(?m)^systemd (\d+)(?:\.\d+)?(?:\s+.*)?$", text)
    }
    facts: dict[str, object] = {"version": _single(versions, "systemd version")}
    for key in (
        "Type",
        "KillMode",
        "Restart",
        "RestartUSec",
        "TimeoutStopUSec",
        "Slice",
        "MainPID",
        "ControlGroup",
        "LoadState",
        "ActiveState",
        "SubState",
    ):
        values: set[object] = set(properties.get(key, set()))
        if key == "MainPID":
            parsed: set[object] = set()
            for value in values:
                if isinstance(value, str) and value.isdecimal():
                    parsed.add(int(value))
            values = parsed
        elif key == "ControlGroup":
            values = {
                value if value == "/system.slice/ac50.service" else "other-redacted"
                for value in values
            }
        elif key == "Slice":
            values = {value if value == "system.slice" else "other-redacted" for value in values}
        facts[key] = _single(values, key)
    exec_values = properties.get("ExecStart", set())
    facts["exec_start_stock"] = _single(
        {"/usr/bin/MPC Sample" in value for value in exec_values}, "stock ExecStart identity"
    )
    for key in ("User", "Group"):
        values = properties.get(key, set())
        normalized = {
            "default-root" if value == "" else value if value.isdecimal() or value == "root" else "named-redacted"
            for value in values
        }
        facts[key] = _single(set(normalized), key)
    facts["dependency_properties_present"] = _fact(
        "confirmed" if any(properties.get(key) for key in ("Requires", "Wants", "After", "Before")) else "unknown",
        any(properties.get(key) for key in ("Requires", "Wants", "After", "Before")),
        "selected dependency properties only",
    )
    return {"facts": facts, "unexpected_fields": unexpected, "malformed_lines": malformed}


def _proc_stat(line: str) -> tuple[int, int] | None:
    match = re.fullmatch(r"(\d+) \(.*\) (.+)", line.strip())
    if not match:
        return None
    fields = match.group(2).split()
    if len(fields) < 20 or not fields[19].isdecimal():
        return None
    return int(match.group(1)), int(fields[19])


def analyze_process(text: str) -> dict[str, object]:
    pids: set[object] = set()
    ppids: set[object] = set()
    uids: set[object] = set()
    gids: set[object] = set()
    starts: set[object] = set()
    executables: set[object] = set()
    parent_pids: set[int] = set()
    malformed = 0
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        stat = _proc_stat(stripped)
        if stat:
            pids.add(stat[0])
            starts.add(stat[1])
            continue
        match = re.fullmatch(r"(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(.+)", stripped)
        if match:
            pid = int(match.group(1))
            ppid = int(match.group(2))
            command = match.group(5)
            if command == "MPC Sample" or command.startswith("/usr/bin/MPC Sample"):
                pids.add(pid)
                ppids.add(ppid)
                uids.add(int(match.group(3)))
                gids.add(int(match.group(4)))
                if command.startswith("/"):
                    executables.add(command == "/usr/bin/MPC Sample")
            else:
                parent_pids.add(pid)
            continue
        match = re.fullmatch(r"Uid:\s+(\d+)(?:\s+\d+){3}", stripped)
        if match:
            uids.add(int(match.group(1)))
            continue
        match = re.fullmatch(r"Gid:\s+(\d+)(?:\s+\d+){3}", stripped)
        if match:
            gids.add(int(match.group(1)))
            continue
        if stripped.startswith("/"):
            executables.add(stripped == "/usr/bin/MPC Sample")
            continue
        malformed += 1
    return {
        "facts": {
            "pid": _single(pids, "process PID"),
            "ppid": _single(ppids, "parent PID"),
            "uid": _single(uids, "process UID"),
            "gid": _single(gids, "process GID"),
            "start_ticks": _single(starts, "Linux process start token"),
            "exact_stock_executable": _single(executables, "exact executable path"),
            "parent_row_matches_ppid": _fact(
                "confirmed" if len(ppids) == 1 and parent_pids else "unknown",
                len(ppids) == 1 and parent_pids == ppids,
                "bounded parent process row compared with stock PPID",
            ),
        },
        "malformed_lines": malformed,
    }


def analyze_cgroup(text: str) -> dict[str, object]:
    groups: set[str] = set()
    malformed = 0
    for line in text.splitlines():
        if not line.strip():
            continue
        match = re.fullmatch(r"\d+:[^:]*:(/[A-Za-z0-9_./@:-]+)", line.strip())
        if match:
            groups.add(match.group(1))
        else:
            malformed += 1
    safe = {
        value if re.fullmatch(r"/system\.slice/ac50\.service", value) else "other-redacted"
        for value in groups
    }
    return {"facts": {"membership": _single(set(safe), "cgroup membership")}, "malformed_lines": malformed}


def analyze_mount(text: str) -> dict[str, object]:
    run_types: set[object] = set()
    run_rw: set[object] = set()
    run_nosuid: set[object] = set()
    modes: set[object] = set()
    runtime_unit_modes: set[object] = set()
    owner_uids: set[object] = set()
    owner_gids: set[object] = set()
    malformed = 0
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        if stripped.startswith("RUN_MODE=") and stripped[9:].isdecimal():
            modes.add(stripped[9:])
        elif stripped.startswith("RUNTIME_UNIT_DIR_MODE=") and stripped[22:].isdecimal():
            runtime_unit_modes.add(stripped[22:])
        elif stripped.startswith("RUN_UID=") and stripped[8:].isdecimal():
            owner_uids.add(int(stripped[8:]))
        elif stripped.startswith("RUN_GID=") and stripped[8:].isdecimal():
            owner_gids.add(int(stripped[8:]))
        elif "/run" in stripped:
            fields = stripped.replace('"', "").split()
            run_types.update(field for field in fields if field in {"tmpfs", "ramfs"})
            options = next((field for field in fields if "," in field or field in {"rw", "ro"}), "")
            option_set = set(options.split(","))
            run_rw.add("rw" in option_set and "ro" not in option_set)
            run_nosuid.add("nosuid" in option_set)
        else:
            malformed += 1
    return {
        "facts": {
            "run_filesystem": _single(run_types, "/run filesystem type"),
            "run_read_write": _single(run_rw, "/run read/write mount state"),
            "run_nosuid": _single(run_nosuid, "/run nosuid state"),
            "run_mode": _single(modes, "/run mode"),
            "runtime_unit_dir_mode": _single(runtime_unit_modes, "runtime unit directory mode"),
            "run_owner_uid": _single(owner_uids, "/run owner UID"),
            "run_owner_gid": _single(owner_gids, "/run owner GID"),
        },
        "malformed_lines": malformed,
    }


def analyze_network(text: str) -> dict[str, object]:
    expected_interfaces: set[str] = set()
    expected_routes: set[str] = set()
    up_interfaces: set[str] = set()
    other_addresses = 0
    for line in text.splitlines():
        link = re.match(r"\d+:\s+([A-Za-z0-9_.-]+):?.*(?:state\s+UP|<[^>]*\bUP\b[^>]*>)", line)
        if link:
            up_interfaces.add(link.group(1).rstrip(":"))
        match = re.search(r"(?:^\d+:\s+|^)([A-Za-z0-9_.-]+).*?\binet\s+(\d{1,3}(?:\.\d{1,3}){3})/(\d{1,2})", line)
        if match:
            interface, address, prefix = match.groups()
            if address == EXPECTED_ADDRESS:
                expected_interfaces.add(interface)
            else:
                other_addresses += 1
            if " UP " in f" {line} " or "state UP" in line:
                up_interfaces.add(interface)
        route = re.search(r"192\.168\.155\.0/28\s+dev\s+([A-Za-z0-9_.-]+)", line)
        if route:
            expected_routes.add(route.group(1))
    relation = (
        "matching"
        if len(expected_interfaces) == 1 and expected_interfaces == expected_routes
        else "conflicting"
        if expected_interfaces and expected_routes and expected_interfaces != expected_routes
        else None
    )
    return {
        "facts": {
            "expected_address_interface": _single(set(expected_interfaces), "expected USB address assignment"),
            "expected_subnet_route_interface": _single(set(expected_routes), "expected USB subnet route"),
            "address_route_relation": _fact(
                "confirmed" if relation == "matching" else "conflicting" if relation == "conflicting" else "unknown",
                relation,
                "address and route interface comparison",
            ),
            "expected_interface_up": _fact(
                "confirmed" if len(expected_interfaces) == 1 else "unknown",
                bool(expected_interfaces & up_interfaces),
                "interface state from supplied address evidence",
            ),
        },
        "other_address_count": other_addresses,
    }


def analyze_sockets(text: str) -> dict[str, object]:
    exact_ports: set[int] = set()
    wildcard_ports: set[int] = set()
    other_count = 0
    for line in text.splitlines():
        matches = re.findall(r"(?:\[([^]]+)\]|(\d{1,3}(?:\.\d{1,3}){3})|([*])):(\d{1,5})", line)
        if not matches:
            continue
        ipv6, ipv4, wildcard, port_text = matches[-1]
        port = int(port_text)
        address = ipv6 or ipv4 or wildcard
        if address == EXPECTED_ADDRESS:
            exact_ports.add(port)
        elif address in {"*", "0.0.0.0", "::"}:
            wildcard_ports.add(port)
        else:
            other_count += 1
    return {
        "facts": {
            "expected_address_ports": _fact("confirmed" if exact_ports else "unknown", sorted(exact_ports), "listeners on expected address"),
            "wildcard_ports": _fact("confirmed" if wildcard_ports else "unknown", sorted(wildcard_ports), "wildcard listeners"),
        },
        "other_listener_count": other_count,
    }


def analyze_firewall(text: str) -> dict[str, object]:
    lowered = text.lower()
    mentions_usb = EXPECTED_ADDRESS in text or EXPECTED_SUBNET in text or "cdc" in lowered
    accepts = bool(re.search(r"\baccept\b", lowered)) and mentions_usb
    drops = bool(re.search(r"\b(drop|reject)\b", lowered)) and mentions_usb
    classification = "conflicting" if accepts and drops else "inferred" if accepts or drops else "unknown"
    return {
        "facts": {
            "usb_relevant_rules": _fact(
                classification,
                "mixed" if accepts and drops else "accept-mentioned" if accepts else "drop-mentioned" if drops else None,
                "keyword-only firewall summary; rule semantics require review",
            )
        }
    }


def analyze_stock(text: str) -> dict[str, object]:
    allowed = {"UI", "AUDIO", "CONTROLS", "PID_BEFORE", "PID_AFTER", "START_BEFORE", "START_AFTER"}
    values: dict[str, set[str]] = {}
    unexpected: set[str] = set()
    malformed = 0
    for line in text.splitlines():
        if not line.strip():
            continue
        if "=" not in line:
            malformed += 1
            continue
        key, value = line.split("=", 1)
        if key not in allowed:
            unexpected.add(key[:64])
        else:
            values.setdefault(key, set()).add(value.strip())
    normal = {
        key: _single(
            {value if value in {"normal", "abnormal"} else "unexpected-redacted" for value in values.get(key, set())},
            key,
        )
        for key in ("UI", "AUDIO", "CONTROLS")
    }
    pid_same = values.get("PID_BEFORE") == values.get("PID_AFTER") and bool(values.get("PID_BEFORE"))
    start_same = values.get("START_BEFORE") == values.get("START_AFTER") and bool(values.get("START_BEFORE"))
    normal["identity_continuity"] = _fact(
        "confirmed" if pid_same and start_same else "conflicting" if values and not (pid_same and start_same) else "unknown",
        pid_same and start_same,
        "bounded before/after PID and start-token observation",
    )
    return {"facts": normal, "unexpected_fields": sorted(unexpected), "malformed_lines": malformed}


ANALYZERS: dict[str, Callable[[str], dict[str, object]]] = {
    "systemd": analyze_systemd,
    "process": analyze_process,
    "cgroup": analyze_cgroup,
    "mount": analyze_mount,
    "network": analyze_network,
    "sockets": analyze_sockets,
    "firewall": analyze_firewall,
    "stock": analyze_stock,
}


def _analyses(report: dict[str, dict[str, object]], category: str) -> list[dict[str, object]]:
    item = report[category]
    analysis = item.get("analysis")
    return [analysis] if isinstance(analysis, dict) else []


def _confirmed_value(analyses: list[dict[str, object]], name: str, value: object | None = None) -> bool:
    facts = [analysis.get("facts", {}).get(name, {}) for analysis in analyses]
    return any(
        fact.get("classification") == "confirmed" and (value is None or fact.get("value") == value)
        for fact in facts
    )


def decision_gates(report: dict[str, dict[str, object]]) -> dict[str, dict[str, str]]:
    systemd = _analyses(report, "systemd")
    process = _analyses(report, "process")
    mount = _analyses(report, "mount")
    network = _analyses(report, "network")
    stock = _analyses(report, "stock")
    systemd_baseline = (
        _confirmed_value(systemd, "version")
        and _confirmed_value(systemd, "Type")
        and _confirmed_value(systemd, "KillMode")
    )
    run_tmpfs = _confirmed_value(mount, "run_filesystem", "tmpfs")
    runtime_unit_dir = _confirmed_value(mount, "runtime_unit_dir_mode")
    process_uid = _confirmed_value(process, "uid") and not _confirmed_value(process, "uid", 0)
    usb_mapping = _confirmed_value(
        network, "address_route_relation", "matching"
    ) and _confirmed_value(network, "expected_interface_up", True)
    continuity = _confirmed_value(stock, "identity_continuity", True)

    def gate(status: str, reason: str, phase: str) -> dict[str, str]:
        return {"status": status, "reason": reason, "phase": phase}

    return {
        "independent_systemd_unit_feasibility": gate(
            "inferred" if systemd_baseline and run_tmpfs and runtime_unit_dir else "unknown",
            "passive facts can support design review but cannot prove a new unit starts independently",
            "P3.2a then P3.2b",
        ),
        "survival_across_ac50_restart": gate("unknown", "requires the zero-hook active lifecycle experiment", "P3.2b"),
        "least_privilege_snapshot_access": gate(
            "inferred" if process_uid and run_tmpfs else "unknown",
            "stock-only operation has no B0.2 snapshot; ownership must be demonstrated later",
            "P3.2a plus later zero-hook fixture",
        ),
        "constructor_report_accessibility": gate("unknown", "constructor report is absent during stock-only operation", "later zero-hook fixture"),
        "usb_only_network_binding": gate(
            "inferred" if usb_mapping else "unknown",
            "address assignment does not prove successful or exclusive bind",
            "P3.2a then P3.2b/P3.3",
        ),
        "firewall_routing_isolation": gate("unknown", "static route/rule text cannot prove end-to-end isolation", "P3.2a review then authorized test"),
        "pid_start_token_continuity": gate(
            "confirmed" if continuity else "unknown",
            "confirmation applies only to the bounded passive observation window",
            "P3.2a; restart continuity requires P3.2b",
        ),
        "runtime_only_cleanup": gate(
            "inferred" if run_tmpfs else "unknown",
            "tmpfs supports reboot cleanup but does not prove unit/artifact cleanup after exit",
            "P3.2a then P3.2b",
        ),
        "stock_application_stability": gate(
            "confirmed"
            if all(_confirmed_value(stock, name, "normal") for name in ("UI", "AUDIO", "CONTROLS"))
            else "unknown",
            "bounded operator observations only; active experiment requires a fresh regression",
            "P3.2a and P3.2b",
        ),
    }


def build_report(inputs: dict[str, list[Path]]) -> dict[str, object]:
    report: dict[str, object] = {}
    missing: list[str] = []
    for category in CATEGORIES:
        sources: list[dict[str, object]] = []
        texts: list[str] = []
        for path in inputs.get(category, []):
            text, evidence = read_text(path)
            texts.append(text)
            sources.append(evidence)
        report[category] = {
            "sources": sources,
            "analysis": ANALYZERS[category]("\n".join(texts)) if texts else None,
        }
        if not sources:
            missing.append(category)
    typed_report: dict[str, dict[str, object]] = {}
    for category in CATEGORIES:
        item = report[category]
        if not isinstance(item, dict):
            raise AssertionError("category report must be an object")
        typed_report[category] = item
    report["decision_gates"] = decision_gates(typed_report)
    report["missing_evidence_categories"] = missing
    report["limitations"] = [
        "offline analysis confirms only facts present in supplied evidence",
        "absence of evidence never proves absence of a service or capability",
        "inferred gates are not passed gates",
        "restart survival and cleanup require separately authorized P3.2b execution",
    ]
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for category in CATEGORIES:
        parser.add_argument(f"--{category}", type=Path, action="append", default=[])
    args = parser.parse_args()
    inputs = {category: getattr(args, category) for category in CATEGORIES}
    print(json.dumps(build_report(inputs), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
