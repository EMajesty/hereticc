#!/usr/bin/env python3
"""Offline, bounded inspection of saved MPC Sample USB/HTTP evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path

MAX_INPUT_BYTES = 32 * 1024 * 1024
API_PATH = re.compile(r"/(?:api/v1/[A-Za-z0-9_?&=./{}:$-]*|version\.json|software-update)")
HTTP_CALL = re.compile(
    r"(?P<prefix>fetch|axios\.(?P<axios>get|post|put|delete|patch)|\.open)"
    r"\s*\(\s*(?:(?P<quote>['\"])(?P<path>/[^'\"]+)(?P=quote))?",
    re.IGNORECASE,
)
METHOD_NEAR = re.compile(r"method\s*:\s*['\"](GET|POST|PUT|DELETE|PATCH)['\"]", re.IGNORECASE)
SCRIPT_SRC = re.compile(r"<script\b[^>]*\bsrc\s*=\s*['\"]([^'\"]+)['\"]", re.IGNORECASE)
WEBSOCKET = re.compile(r"\b(?:new\s+)?WebSocket\s*\(")


def read_text(path: Path) -> tuple[str, dict[str, object]]:
    data = path.read_bytes()
    if len(data) > MAX_INPUT_BYTES:
        raise ValueError(f"input exceeds {MAX_INPUT_BYTES} bytes: {path}")
    return data.decode("utf-8", errors="replace"), {
        "name": path.name,
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
    }


def analyze_headers(text: str) -> dict[str, object]:
    lines = [line.rstrip("\r") for line in text.splitlines()]
    status = lines[0] if lines and lines[0].startswith("HTTP/") else None
    allowed = {"server", "content-type", "content-length", "location"}
    headers: dict[str, str] = {}
    for line in lines[1:]:
        if ":" not in line:
            continue
        name, value = line.split(":", 1)
        if name.lower() in allowed:
            headers[name.lower()] = value.strip()
    return {"status": status, "headers": headers}


def analyze_html(text: str) -> dict[str, object]:
    return {"script_sources": sorted(set(SCRIPT_SRC.findall(text)))}


def _method_for_call(match: re.Match[str], text: str) -> str:
    if match.group("axios"):
        return match.group("axios").upper()
    nearby = text[match.start() : min(len(text), match.start() + 240)]
    method = METHOD_NEAR.search(nearby)
    return method.group(1).upper() if method else "GET_OR_UNRESOLVED"


def analyze_javascript(text: str) -> dict[str, object]:
    direct: list[dict[str, str]] = []
    direct_paths: set[str] = set()
    for match in HTTP_CALL.finditer(text):
        path = match.group("path")
        if path and API_PATH.fullmatch(path):
            direct.append({"path": path, "method": _method_for_call(match, text)})
            direct_paths.add(path)
    literals = sorted(set(API_PATH.findall(text)))
    inventory = [
        {
            "path": path,
            "classification": "direct-static-call" if path in direct_paths else "literal-only",
            "occurrences": text.count(path),
        }
        for path in literals
    ]
    return {
        "endpoint_inventory": inventory,
        "direct_static_calls": direct,
        "websocket_constructor_count": len(WEBSOCKET.findall(text)),
        "limitations": [
            "literal-only does not prove endpoint implementation or runtime reachability",
            "direct-static-call does not prove the rendered code path executes",
            "computed/minified call paths may not be resolved",
        ],
    }


def analyze_usb_text(text: str) -> dict[str, object]:
    lowered = text.lower()
    vids = sorted(set(re.findall(r"(?:idvendor|vendor id)\s*[:=]?\s*(?:0x)?([0-9a-f]{4})", lowered)))
    pids = sorted(set(re.findall(r"(?:idproduct|product id)\s*[:=]?\s*(?:0x)?([0-9a-f]{4})", lowered)))
    return {
        "vendor_ids": vids,
        "product_ids": pids,
        "mentions_cdc_ncm": "cdc_ncm" in lowered or "cdc ncm" in lowered,
        "mentions_audio": "audio" in lowered,
        "mentions_midi": "midi" in lowered,
    }


def analyze_network_text(text: str) -> dict[str, object]:
    ipv4 = sorted(set(re.findall(r"(?<![0-9.])(?:\d{1,3}\.){3}\d{1,3}(?:/\d{1,2})?", text)))
    return {"ipv4_literals": ipv4, "mentions_dhcp": "dhcp" in text.lower()}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--headers", type=Path, action="append", default=[])
    parser.add_argument("--html", type=Path, action="append", default=[])
    parser.add_argument("--javascript", type=Path, action="append", default=[])
    parser.add_argument("--usb", type=Path, action="append", default=[])
    parser.add_argument("--network", type=Path, action="append", default=[])
    args = parser.parse_args()

    report: dict[str, list[dict[str, object]]] = {}
    groups = (
        ("headers", args.headers, analyze_headers),
        ("html", args.html, analyze_html),
        ("javascript", args.javascript, analyze_javascript),
        ("usb", args.usb, analyze_usb_text),
        ("network", args.network, analyze_network_text),
    )
    for name, paths, analyzer in groups:
        report[name] = []
        for path in paths:
            text, evidence = read_text(path)
            report[name].append({"evidence": evidence, "analysis": analyzer(text)})
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
