#!/usr/bin/env python3
"""Offline, bounded inspection of saved MPC Sample USB/HTTP evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path
from urllib.parse import urlsplit

MAX_INPUT_BYTES = 32 * 1024 * 1024
INTERESTING_PATH = re.compile(
    r"/(?:api/v1|object-(?:meta|properties|method)|subtree|version\.json|software-update)"
    r"[A-Za-z0-9_?&=./{}:$-]*"
)
STRING_LITERAL = re.compile(r"(?P<quote>['\"])(?P<value>(?:\\.|(?!\1).)*?)(?P=quote)")
TEMPLATE_LITERAL = re.compile(r"`(?P<value>(?:\\.|[^`])*)`")
METHOD_NEAR = re.compile(r"method\s*:\s*['\"](GET|POST|PUT|DELETE|PATCH)['\"]", re.IGNORECASE)
SCRIPT_SRC = re.compile(r"<script\b[^>]*\bsrc\s*=\s*['\"]([^'\"]+)['\"]", re.IGNORECASE)
WEBSOCKET = re.compile(r"\bnew\s+WebSocket\s*\(\s*(?P<expression>[^,)]+)")
FETCH = re.compile(r"\bfetch\s*\(\s*(?P<expression>[^,)]{1,200})")
PROPERTY_STRING = re.compile(
    r"(?P<key>[A-Za-z_$][\w$]*)\s*:\s*(?P<quote>['\"])(?P<value>(?:\\.|(?!\2).)*?)(?P=quote)"
)
BASE_DECLARATION = re.compile(
    r"(?P<key>baseURL|baseUrl|wsHost)\s*:\s*(?P<expression>[^,}]{1,240})"
)
CONCAT_CHAIN = re.compile(r"(?P<chain>(?:['\"]{2}|[A-Za-z_$][\w$]*)\.concat\([^;]{1,500}?\))")
PROTOTYPE_METHOD = re.compile(
    r"\.prototype\.(?P<name>[A-Za-z_$][\w$]*)\s*=\s*function\([^)]*\)\s*\{"
    r"(?P<body>.{0,1800}?)(?=,[A-Za-z_$][\w$]*\.prototype\.|$)"
)
INITIALIZE_CALL = re.compile(r"\.initialize\s*\(")
INITIALIZE_DEFINITION = re.compile(r"\.prototype\.initialize\s*=\s*function")
CLASSIFICATION_ORDER = {
    "unknown": 0,
    "literal-or-constructed-reference": 1,
    "client-library-definition": 2,
    "static-application-call": 3,
    "observed-active-endpoint": 4,
}


def read_text(path: Path) -> tuple[str, dict[str, object]]:
    data = path.read_bytes()
    if len(data) > MAX_INPUT_BYTES:
        raise ValueError(f"input exceeds {MAX_INPUT_BYTES} bytes")
    text = data.decode("utf-8", errors="replace")
    return text, {
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "utf8_replacement_characters": text.count("\ufffd"),
    }


def analyze_headers(text: str) -> dict[str, object]:
    lines = [line.rstrip("\r") for line in text.splitlines()]
    status = lines[0] if lines and re.fullmatch(r"HTTP/\d(?:\.\d)? \d{3}[^\r\n]*", lines[0]) else None
    allowed = {"server", "content-type", "content-length"}
    headers: dict[str, str] = {}
    for line in lines[1:]:
        if ":" not in line:
            continue
        name, value = line.split(":", 1)
        if name.lower() in allowed:
            headers[name.lower()] = value.strip()[:256]
    return {"status": status, "headers": headers}


def _safe_asset_path(value: str) -> str | None:
    parsed = urlsplit(value)
    if parsed.scheme not in {"", "http", "https"}:
        return None
    path = parsed.path
    return path if path and len(path) <= 512 else None


def analyze_html(text: str) -> dict[str, object]:
    sources = (_safe_asset_path(value) for value in SCRIPT_SRC.findall(text))
    return {"script_sources": sorted({source for source in sources if source})}


def _without_comments(text: str) -> str:
    """Mask ordinary comments without treating URL text as line comments."""

    def mask(match: re.Match[str]) -> str:
        return "".join("\n" if char == "\n" else " " for char in match.group(0))

    code = re.sub(r"/\*.*?\*/", mask, text, flags=re.DOTALL)
    return re.sub(r"(?m)(?P<prefix>^|[;])(?P<space>[ \t]*)//[^\r\n]*", mask, code)


def _paths(expression: str) -> set[str]:
    return {re.sub(r"\$\{[^}]*\}", "", path) for path in INTERESTING_PATH.findall(expression)}


def _targets(expression: str) -> set[str]:
    targets = _paths(expression)
    for value in re.findall(r"https?://[^'\"`\s,)}]+", expression):
        parsed = urlsplit(value)
        targets.add(f"{parsed.scheme}://{parsed.netloc}{parsed.path}")
    return targets


def _method(expression: str, default: str = "GET_OR_UNRESOLVED") -> str:
    match = METHOD_NEAR.search(expression)
    return match.group(1).upper() if match else default


def _inside_span(position: int, spans: list[tuple[int, int]]) -> bool:
    return any(start <= position < end for start, end in spans)


def _add_finding(
    findings: dict[tuple[str, str], dict[str, object]],
    path: str,
    classification: str,
    evidence: str,
    method: str = "UNKNOWN",
) -> None:
    key = (method, path)
    current = findings.get(key)
    item = {"path": path, "method": method, "classification": classification, "evidence": [evidence]}
    if current is None:
        findings[key] = item
    elif CLASSIFICATION_ORDER[classification] > CLASSIFICATION_ORDER[str(current["classification"])]:
        item["evidence"] = sorted(set([*current["evidence"], evidence]))
        findings[key] = item
    else:
        current["evidence"] = sorted(set([*current["evidence"], evidence]))


def analyze_javascript(text: str, observed: list[str] | None = None) -> dict[str, object]:
    code = _without_comments(text)
    findings: dict[tuple[str, str], dict[str, object]] = {}
    properties: dict[str, str] = {}
    literal_spans = [match.span() for match in STRING_LITERAL.finditer(code)]
    literal_spans.extend(match.span() for match in TEMPLATE_LITERAL.finditer(code))

    for match in PROPERTY_STRING.finditer(code):
        value = match.group("value")
        targets = _targets(value)
        if targets:
            properties[match.group("key")] = sorted(targets)[0]
            for path in targets:
                _add_finding(findings, path, "literal-or-constructed-reference", "static endpoint property")

    for match in STRING_LITERAL.finditer(code):
        for path in _targets(match.group("value")):
            _add_finding(findings, path, "literal-or-constructed-reference", "string literal")
    for match in TEMPLATE_LITERAL.finditer(code):
        for path in _targets(match.group("value")):
            _add_finding(findings, path, "literal-or-constructed-reference", "template literal")
    for match in CONCAT_CHAIN.finditer(code):
        for path in _paths(match.group("chain")):
            _add_finding(findings, path, "literal-or-constructed-reference", "concat URL construction")

    static_calls: list[dict[str, str]] = []
    called_targets: set[str] = set()
    for match in FETCH.finditer(code):
        if _inside_span(match.start(), literal_spans):
            continue
        expression = match.group("expression").strip()
        call_end = code.find(")", match.end())
        call_end = min(len(code), match.end() + 300) if call_end < 0 else call_end + 1
        context = code[match.start() : call_end]
        paths = _targets(expression)
        property_match = re.fullmatch(r"[A-Za-z_$][\w$]*(?:\.[A-Za-z_$][\w$]*)+", expression)
        if property_match:
            leaf = expression.rsplit(".", 1)[-1]
            if leaf in properties:
                paths.update(_targets(properties[leaf]))
        method = _method(context)
        for path in paths:
            called_targets.add(path)
            _add_finding(findings, path, "static-application-call", "fetch call site", method)
            static_calls.append({"path": path, "method": method, "expression_kind": "resolved-static"})

    client_methods: list[dict[str, object]] = []
    for match in PROTOTYPE_METHOD.finditer(code):
        body = match.group("body")
        paths = sorted(_paths(body))
        if not paths and not re.search(r"fetch\s*\(|WebSocket", body):
            continue
        methods = sorted(set(METHOD_NEAR.findall(body)))
        if not methods:
            methods = ["GET" if match.group("name").startswith("get") else "GET_OR_UNRESOLVED"]
        client_methods.append({"name": match.group("name"), "paths": paths, "http_methods": methods})
        for path in paths:
            for method in methods:
                _add_finding(findings, path, "client-library-definition", match.group("name"), method)

    for item in observed or []:
        parts = item.strip().split(maxsplit=1)
        if len(parts) != 2 or not re.fullmatch(r"GET|POST|PUT|DELETE|PATCH", parts[0].upper()):
            raise ValueError(f"invalid observed endpoint: {item}")
        _add_finding(findings, parts[1], "observed-active-endpoint", "operator-supplied runtime evidence", parts[0].upper())

    base_urls = [
        {"key": match.group("key"), "expression": match.group("expression")[:240]}
        for match in BASE_DECLARATION.finditer(code)
    ]
    websocket_sites = [match.group("expression").strip()[:200] for match in WEBSOCKET.finditer(code)]
    initialize_definitions = len(INITIALIZE_DEFINITION.findall(code))
    initialize_calls = len(INITIALIZE_CALL.findall(code))

    return {
        "endpoint_inventory": sorted(findings.values(), key=lambda item: (str(item["path"]), str(item["method"]))),
        "static_application_calls": static_calls,
        "endpoint_properties": dict(
            sorted(
                (key, value)
                for key, value in properties.items()
                if _paths(value) or value in called_targets
            )
        ),
        "base_url_declarations": base_urls,
        "client_library_methods": client_methods,
        "websocket_constructor_sites": websocket_sites,
        "client_initialize_definition_count": initialize_definitions,
        "client_initialize_invocation_reference_count": initialize_calls,
        "limitations": [
            "observed-active-endpoint requires explicit operator-supplied runtime evidence",
            "static-application-call does not prove runtime execution or server implementation",
            "client-library-definition does not prove construction, initialization, or use",
            "computed aliases and general JavaScript data flow are not resolved",
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
    parser.add_argument("--observed", action="append", default=[], metavar="'METHOD /path'")
    args = parser.parse_args()

    report: dict[str, list[dict[str, object]]] = {}
    groups = (
        ("headers", args.headers, analyze_headers),
        ("html", args.html, analyze_html),
        ("javascript", args.javascript, lambda text: analyze_javascript(text, args.observed)),
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
