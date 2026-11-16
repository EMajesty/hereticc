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
MAX_COLLECTION_BYTES = 4096
MAX_COLLECTION_FIELDS = 32
MAX_COLLECTION_VALUE_BYTES = 128
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
    "collection",
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
    if len(ppids) != 1 or not parent_pids:
        parent_match = _fact(
            "unknown", None, "missing or ambiguous stock PPID or parent process row"
        )
    elif parent_pids == ppids:
        parent_match = _fact(
            "confirmed", True, "bounded parent process row matches stock PPID"
        )
    else:
        parent_match = _fact(
            "conflicting", False, "parent process rows contradict stock PPID"
        )
    return {
        "facts": {
            "pid": _single(pids, "process PID"),
            "ppid": _single(ppids, "parent PID"),
            "uid": _single(uids, "process UID"),
            "gid": _single(gids, "process GID"),
            "start_ticks": _single(starts, "Linux process start token"),
            "exact_stock_executable": _single(executables, "exact executable path"),
            "parent_row_matches_ppid": parent_match,
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
    expected_addresses: set[tuple[str, int]] = set()
    expected_routes: set[str] = set()
    link_states: dict[str, set[bool]] = {}
    other_addresses = 0
    for line in text.splitlines():
        link = re.match(r"\d+:\s+([A-Za-z0-9_.-]+):\s+<([^>]*)>.*\bstate\s+(UP|DOWN|UNKNOWN)\b", line)
        if link and " inet " not in f" {line} ":
            interface, flags, state = link.groups()
            link_states.setdefault(interface, set()).add(
                state == "UP" or "UP" in flags.split(",")
            )
        match = re.search(r"(?:^\d+:\s+|^)([A-Za-z0-9_.-]+).*?\binet\s+(\d{1,3}(?:\.\d{1,3}){3})/(\d{1,2})", line)
        if match:
            interface, address, prefix = match.groups()
            if address == EXPECTED_ADDRESS:
                expected_addresses.add((interface, int(prefix)))
            else:
                other_addresses += 1
        route = re.search(r"192\.168\.155\.0/28\s+dev\s+([A-Za-z0-9_.-]+)", line)
        if route:
            expected_routes.add(route.group(1))
    correct_interfaces = {interface for interface, prefix in expected_addresses if prefix == 28}
    wrong_prefix = any(prefix != 28 for _, prefix in expected_addresses)
    address_conflict = wrong_prefix or len(correct_interfaces) > 1
    route_conflict = len(expected_routes) > 1
    if address_conflict or route_conflict:
        relation = "conflicting"
    elif len(correct_interfaces) == 1 and len(expected_routes) == 1:
        relation = "matching" if correct_interfaces == expected_routes else "conflicting"
    else:
        relation = None

    if address_conflict:
        address_fact = _fact(
            "conflicting", None, "expected address has wrong /28 prefix or multiple interfaces"
        )
    else:
        address_fact = _single(set(correct_interfaces), "expected USB /28 address assignment")

    if len(correct_interfaces) != 1 or address_conflict:
        up_fact = _fact("unknown", None, "expected USB interface is not uniquely established")
    else:
        interface = next(iter(correct_interfaces))
        states = link_states.get(interface, set())
        if not states:
            up_fact = _fact("unknown", None, "missing explicit link-state evidence")
        elif len(states) > 1:
            up_fact = _fact("conflicting", None, "contradictory link-state observations")
        else:
            up_fact = _fact("confirmed", next(iter(states)), "explicit link-state evidence")
    return {
        "facts": {
            "expected_address_interface": address_fact,
            "expected_subnet_route_interface": _single(set(expected_routes), "expected USB subnet route"),
            "address_route_relation": _fact(
                "confirmed" if relation == "matching" else "conflicting" if relation == "conflicting" else "unknown",
                relation,
                "address and route interface comparison",
            ),
            "expected_interface_up": up_fact,
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


COLLECTION_FIELDS = {
    "evidence_schema",
    "collection_phase",
    "collection_status",
    "restart_observed",
    "systemd_version",
    "service_type",
    "service_kill_mode",
    "service_restart",
    "service_main_pid",
    "service_cgroup",
    "process_executable",
    "process_pid",
    "process_parent_pid",
    "process_start_ticks",
    "process_uid",
    "process_gid",
    "process_cgroup",
    "run_filesystem",
    "run_read_write",
    "run_mode",
    "runtime_unit_dir_mode",
    "usb_interface",
    "usb_ipv4",
    "usb_link_up",
    "usb_route",
    "usb_route_interface",
    "listener_exact_ports",
    "listener_wildcard_ports",
}
COLLECTION_REQUIRED = {
    "evidence_schema",
    "collection_phase",
    "collection_status",
    "restart_observed",
}


def analyze_collection(text: str) -> dict[str, object]:
    """Validate the proposed bounded P3.2c selected-fact report."""
    encoded = text.encode("utf-8")
    values: dict[str, set[str]] = {}
    counts: dict[str, int] = {}
    unexpected: set[str] = set()
    malformed = 0
    oversized = len(encoded) > MAX_COLLECTION_BYTES
    lines = text.splitlines()
    for line in lines:
        if not line or "=" not in line:
            malformed += 1
            continue
        key, value = line.split("=", 1)
        if key not in COLLECTION_FIELDS:
            unexpected.add(key[:64])
            continue
        if len(value.encode("utf-8")) > MAX_COLLECTION_VALUE_BYTES:
            malformed += 1
            continue
        counts[key] = counts.get(key, 0) + 1
        values.setdefault(key, set()).add(value)

    duplicate_conflict = any(count > 1 for count in counts.values())
    missing = sorted(COLLECTION_REQUIRED - values.keys())
    invalid: list[str] = []

    def one(name: str) -> str | None:
        items = values.get(name, set())
        return next(iter(items)) if len(items) == 1 else None

    exact_values = {
        "evidence_schema": {"neurogenic-b1-p3.2c-v1"},
        "collection_phase": {"pre-restart", "constructor", "post-restart"},
        "collection_status": {"complete", "partial", "failed"},
        "restart_observed": {"true", "false"},
        "run_read_write": {"true", "false", "unknown"},
        "usb_link_up": {"true", "false", "unknown"},
        "process_executable": {"stock-exact", "other", "unknown"},
    }
    decimal_fields = {
        "systemd_version",
        "service_main_pid",
        "process_pid",
        "process_parent_pid",
        "process_start_ticks",
        "process_uid",
        "process_gid",
    }
    safe_token_fields = {
        "service_type",
        "service_kill_mode",
        "service_restart",
        "run_filesystem",
        "run_mode",
        "runtime_unit_dir_mode",
        "usb_interface",
        "usb_route_interface",
    }
    for name, allowed in exact_values.items():
        value = one(name)
        if value is not None and value not in allowed:
            invalid.append(name)
    for name in decimal_fields:
        value = one(name)
        if value is not None and not value.isdecimal():
            invalid.append(name)
    for name in safe_token_fields:
        value = one(name)
        if value is not None and not re.fullmatch(r"[A-Za-z0-9_.@:-]{1,64}", value):
            invalid.append(name)
    for name in ("service_cgroup", "process_cgroup"):
        value = one(name)
        if value is not None and not re.fullmatch(r"/[A-Za-z0-9_./@:-]{1,127}", value):
            invalid.append(name)
    if (value := one("usb_ipv4")) is not None and value != f"{EXPECTED_ADDRESS}/28":
        invalid.append("usb_ipv4")
    if (value := one("usb_route")) is not None and value != EXPECTED_SUBNET:
        invalid.append("usb_route")
    for name in ("listener_exact_ports", "listener_wildcard_ports"):
        value = one(name)
        if value is not None and not re.fullmatch(r"none|unknown|\d{1,5}(?:,\d{1,5})*", value):
            invalid.append(name)
        elif value not in {None, "none", "unknown"} and any(
            int(port) > 65535 for port in value.split(",")
        ):
            invalid.append(name)

    contradictions: list[str] = []
    if one("collection_phase") == "pre-restart" and one("restart_observed") == "true":
        contradictions.append("pre-restart phase reports a restart")
    service_pid, process_pid = one("service_main_pid"), one("process_pid")
    if service_pid is not None and process_pid is not None and service_pid != process_pid:
        contradictions.append("service and process PID differ")
    service_group, process_group = one("service_cgroup"), one("process_cgroup")
    if service_group is not None and process_group is not None and service_group != process_group:
        contradictions.append("service and process cgroup differ")
    usb_interface, route_interface = one("usb_interface"), one("usb_route_interface")
    if usb_interface is not None and route_interface is not None and usb_interface != route_interface:
        contradictions.append("USB address and route interfaces differ")

    schema_invalid = (
        oversized
        or len(lines) > MAX_COLLECTION_FIELDS
        or malformed > 0
        or bool(invalid)
        or bool(unexpected)
        or duplicate_conflict
        or bool(missing)
        or (
            one("collection_phase") == "pre-restart"
            and one("restart_observed") != "false"
        )
    )
    schema_classification = "conflicting" if schema_invalid else "confirmed"
    status = one("collection_status")
    completeness_classification = "confirmed" if status in {"complete", "partial", "failed"} else "unknown"
    consistency_classification = "conflicting" if contradictions else "confirmed"

    safe_values = {
        key: next(iter(items))
        for key, items in sorted(values.items())
        if len(items) == 1 and key not in invalid
    }
    return {
        "facts": {
            "schema_validity": _fact(
                schema_classification,
                not schema_invalid,
                "bounded selected-fact P3.2c evidence contract",
            ),
            "collection_completeness": _fact(
                completeness_classification,
                status,
                "producer-declared collection completion status",
            ),
            "baseline_complete": _fact(
                completeness_classification,
                status == "complete",
                "only a complete producer status is eligible for baseline gates",
            ),
            "evidence_consistency": _fact(
                consistency_classification,
                not contradictions,
                "selected cross-field consistency checks",
            ),
        },
        "selected_facts": safe_values,
        "missing_fields": missing,
        "unexpected_fields": sorted(unexpected),
        "invalid_fields": sorted(set(invalid)),
        "contradictions": sorted(contradictions),
        "malformed_lines": malformed,
        "bounds": {
            "bytes_ok": not oversized,
            "field_count_ok": len(lines) <= MAX_COLLECTION_FIELDS,
        },
    }


ANALYZERS: dict[str, Callable[[str], dict[str, object]]] = {
    "systemd": analyze_systemd,
    "process": analyze_process,
    "cgroup": analyze_cgroup,
    "mount": analyze_mount,
    "network": analyze_network,
    "sockets": analyze_sockets,
    "firewall": analyze_firewall,
    "stock": analyze_stock,
    "collection": analyze_collection,
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


def _collection_ready(collection: list[dict[str, object]]) -> bool:
    return (
        _confirmed_value(collection, "schema_validity", True)
        and _confirmed_value(collection, "baseline_complete", True)
        and _confirmed_value(collection, "evidence_consistency", True)
    )


def _collection_values(collection: list[dict[str, object]]) -> dict[str, str]:
    if not collection:
        return {}
    values = collection[0].get("selected_facts", {})
    return values if isinstance(values, dict) else {}


def cross_source_conflicts(report: dict[str, dict[str, object]]) -> list[str]:
    collection = _analyses(report, "collection")
    values = _collection_values(collection)
    if not _confirmed_value(collection, "schema_validity", True):
        return []
    comparisons = (
        ("systemd", "version", "systemd_version", lambda value: str(value)),
        ("systemd", "Type", "service_type", str),
        ("systemd", "KillMode", "service_kill_mode", str),
        ("systemd", "MainPID", "service_main_pid", lambda value: str(value)),
        ("systemd", "ControlGroup", "service_cgroup", str),
        ("process", "pid", "process_pid", lambda value: str(value)),
        ("process", "uid", "process_uid", lambda value: str(value)),
        ("process", "gid", "process_gid", lambda value: str(value)),
        ("cgroup", "membership", "process_cgroup", str),
        ("mount", "run_filesystem", "run_filesystem", str),
        ("mount", "run_mode", "run_mode", str),
        ("mount", "runtime_unit_dir_mode", "runtime_unit_dir_mode", str),
        ("network", "expected_address_interface", "usb_interface", str),
        ("network", "expected_subnet_route_interface", "usb_route_interface", str),
        ("network", "expected_interface_up", "usb_link_up", lambda value: str(value).lower()),
    )
    conflicts = []
    for category, legacy_name, collection_name, normalize in comparisons:
        collection_value = values.get(collection_name)
        if collection_value is None:
            continue
        for analysis in _analyses(report, category):
            fact = analysis.get("facts", {}).get(legacy_name, {})
            if fact.get("classification") == "confirmed" and normalize(fact.get("value")) != collection_value:
                conflicts.append(f"{category}.{legacy_name}!={collection_name}")
    return sorted(set(conflicts))


def decision_gates(report: dict[str, dict[str, object]]) -> dict[str, dict[str, str]]:
    systemd = _analyses(report, "systemd")
    process = _analyses(report, "process")
    mount = _analyses(report, "mount")
    network = _analyses(report, "network")
    stock = _analyses(report, "stock")
    collection = _analyses(report, "collection")
    collection_values = _collection_values(collection)
    collection_ready = _collection_ready(collection)
    conflicts = cross_source_conflicts(report)
    conflict_categories = {item.split(".", 1)[0] for item in conflicts}
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
    collection_systemd = collection_ready and all(
        collection_values.get(name)
        for name in (
            "systemd_version",
            "service_type",
            "service_kill_mode",
            "runtime_unit_dir_mode",
        )
    )
    collection_run = collection_ready and collection_values.get("run_filesystem") == "tmpfs"
    collection_process_uid = (
        collection_ready
        and collection_values.get("process_uid", "0").isdecimal()
        and collection_values.get("process_uid") != "0"
    )
    collection_usb = collection_ready and all(
        (
            collection_values.get("usb_ipv4") == "192.168.155.1/28",
            collection_values.get("usb_route") == "192.168.155.0/28",
            collection_values.get("usb_interface")
            == collection_values.get("usb_route_interface"),
            collection_values.get("usb_link_up") == "true",
        )
    )
    collection_continuity = collection_ready and all(
        collection_values.get(name) for name in ("process_pid", "process_start_ticks")
    )

    def gate(status: str, reason: str, phase: str) -> dict[str, str]:
        return {"status": status, "reason": reason, "phase": phase}

    return {
        "independent_systemd_unit_feasibility": gate(
            "conflicting"
            if {"systemd", "mount"} & conflict_categories
            else "inferred"
            if (systemd_baseline and run_tmpfs and runtime_unit_dir)
            or (collection_systemd and collection_run)
            else "unknown",
            "passive facts can support design review but cannot prove a new unit starts independently",
            "P3.2a then P3.2b",
        ),
        "survival_across_ac50_restart": gate("unknown", "requires the zero-hook active lifecycle experiment", "P3.2b"),
        "least_privilege_snapshot_access": gate(
            "conflicting"
            if {"process", "cgroup", "mount"} & conflict_categories
            else "inferred"
            if (process_uid and run_tmpfs) or (collection_process_uid and collection_run)
            else "unknown",
            "stock-only operation has no B0.2 snapshot; ownership must be demonstrated later",
            "P3.2a plus later zero-hook fixture",
        ),
        "constructor_report_accessibility": gate("unknown", "constructor report is absent during stock-only operation", "later zero-hook fixture"),
        "usb_only_network_binding": gate(
            "conflicting"
            if "network" in conflict_categories
            else "inferred"
            if usb_mapping or collection_usb
            else "unknown",
            "address assignment does not prove successful or exclusive bind",
            "P3.2a then P3.2b/P3.3",
        ),
        "firewall_routing_isolation": gate("unknown", "static route/rule text cannot prove end-to-end isolation", "P3.2a review then authorized test"),
        "pid_start_token_continuity": gate(
            "conflicting"
            if {"systemd", "process", "cgroup"} & conflict_categories
            else "confirmed"
            if continuity or collection_continuity
            else "unknown",
            "confirmation applies only to the bounded passive observation window",
            "P3.2a; restart continuity requires P3.2b",
        ),
        "runtime_only_cleanup": gate(
            "conflicting"
            if "mount" in conflict_categories
            else "inferred"
            if run_tmpfs or collection_run
            else "unknown",
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
    report["cross_source_conflicts"] = cross_source_conflicts(typed_report)
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
