#!/usr/bin/env python3
"""Strict, bounded host controller for the MPC Sample USB development path."""

from __future__ import annotations
import argparse
import dataclasses
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
from typing import Callable, Sequence

SCHEMA = "neurogenic-hil-config-v1"
HOST, PORT, USER = "192.168.155.1", 2222, "root"
CONFIRM = "MPC-SAMPLE-1.3.0.12"
NATIVE_CONFIRM = "INSERT-ONE-UNSAVED-NATIVE-EVENT-1.3.0.12"
GRID_CONFIRM = "PLAYABLE-GRID-ONE-BAR-1.3.0.12"
FIRMWARE_SHA = "44507c7a7d3029c411b2e3780ea286ca3e08b7e3ffd7b5047d52f73bf5299d1f"
ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "device/m03e/scripts/build-package.sh"
HIL_BUILD = ROOT / "device/hil/scripts/build.sh"
M04A_BUILD = ROOT / "device/m04a/scripts/build.sh"
M05A_BUILD = ROOT / "device/m05a/scripts/build.sh"
NORMAL_ARTIFACT = ROOT / "target/m03c/build/neurogenic-m03c-ui.so"
NORMAL_SUMS = ROOT / "target/m03c/build/SHA256SUMS"
HIL_ARTIFACT = ROOT / "target/hil/build/neurogenic-hil-test.so"
HIL_SUMS = ROOT / "target/hil/build/SHA256SUMS"
M04A_ARTIFACT = ROOT / "target/m04a/build/neurogenic-m04a-native-test.so"
M04A_SUMS = ROOT / "target/m04a/build/SHA256SUMS"
M05A_ARTIFACT = ROOT / "target/m05a/build/neurogenic-m05a-native-grid.so"
M05A_SUMS = ROOT / "target/m05a/build/SHA256SUMS"
RUNTIME = "/run/neurogenic-hil"
BUILDS = RUNTIME + "/builds"
ACTIVE = RUNTIME + "/active.sha256"
PREVIOUS = RUNTIME + "/previous.sha256"
DROPIN = "/run/systemd/system/ac50.service.d/neurogenic-hil.conf"
LEGACY_DROPIN_TEXT = "[Service]\nEnvironment=LD_PRELOAD=/run/neurogenic-hil/current.so\n"
MAX_OUTPUT = 8192
RUN_ROOT = ROOT / "target/hil/runs"
RUN_LOCK = ROOT / "target/hil/run.lock"


class HilError(Exception):
    exit_code = 2


class IdentityError(HilError):
    exit_code = 4


class TransportError(HilError):
    exit_code = 5


class RemoteError(HilError):
    exit_code = 6


class RunCycleError(HilError):
    exit_code = 7


@dataclasses.dataclass(frozen=True)
class Config:
    identity: Path
    known_hosts: Path

    @classmethod
    def load(cls, path: Path) -> "Config":
        try:
            raw = path.read_bytes()
        except OSError as exc:
            raise HilError(f"cannot read config: {exc}") from exc
        if len(raw) > 4096:
            raise HilError("config exceeds 4096 bytes")
        try:
            data = json.loads(raw)
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise HilError(f"invalid config JSON: {exc}") from exc
        fields = {"schema", "host", "port", "user", "identity_file", "known_hosts_file"}
        if not isinstance(data, dict) or set(data) != fields or data["schema"] != SCHEMA:
            raise HilError("config does not match the v1 schema")
        if not all(isinstance(data[key], str) for key in ("host", "user", "identity_file", "known_hosts_file")):
            raise HilError("config string fields have invalid types")
        if not isinstance(data["port"], int) or isinstance(data["port"], bool):
            raise HilError("config port has an invalid type")
        if (data["host"], data["port"], data["user"]) != (HOST, PORT, USER):
            raise HilError("only the reviewed USB SSH endpoint is accepted")
        identity, known = Path(data["identity_file"]), Path(data["known_hosts_file"])
        for label, item in (("identity_file", identity), ("known_hosts_file", known)):
            if not item.is_absolute() or not item.is_file() or item.is_symlink():
                raise HilError(f"{label} must be an existing absolute regular file")
        if identity.stat().st_mode & 0o077:
            raise HilError("identity_file must be private (mode 0600 or stricter)")
        return cls(identity, known)


def parse_facts(raw: bytes) -> dict[str, str]:
    if len(raw) > MAX_OUTPUT:
        raise RemoteError("remote output exceeded 8192 bytes")
    try:
        lines = raw.decode("ascii").splitlines()
    except UnicodeDecodeError as exc:
        raise RemoteError("remote output is not ASCII") from exc
    facts: dict[str, str] = {}
    for line in lines:
        if "=" not in line:
            raise RemoteError("malformed remote output")
        key, value = line.split("=", 1)
        if not re.fullmatch(r"[a-z0-9_]+", key) or key in facts or len(value) > 512:
            raise RemoteError("ambiguous remote output")
        facts[key] = value
    return facts


IDENTITY = r"""set -eu
stock='/usr/bin/MPC Sample'; test -f "$stock"
prefix=$(dd if="$stock" bs=4096 count=1 2>/dev/null | sha256sum | cut -d' ' -f1)
pid=$(systemctl show ac50.service --property=MainPID --value)
printf 'schema=neurogenic-hil-device-v1\narchitecture=%s\n' "$(uname -m)"
printf 'firmware_prefix_sha256=%s\n' "$prefix"
printf 'service_load=%s\n' "$(systemctl show ac50.service --property=LoadState --value)"
printf 'service_active=%s\n' "$(systemctl show ac50.service --property=ActiveState --value)"
printf 'service_sub=%s\nmain_pid=%s\n' "$(systemctl show ac50.service --property=SubState --value)" "$pid"
printf 'dropins=%s\n' "$(systemctl show ac50.service --property=DropInPaths --value)"
if test "$pid" -gt 0; then printf 'main_exe=%s\n' "$(readlink "/proc/$pid/exe")"; else printf 'main_exe=\n'; fi
"""

COLLECT = IDENTITY + r"""
if test ! -e '/run/neurogenic-hil' && test ! -L '/run/neurogenic-hil'; then
 printf 'hil_runtime=absent\n'
else
 test -d '/run/neurogenic-hil' && test ! -L '/run/neurogenic-hil'
 printf 'hil_runtime=present\n'
fi
for item in active previous; do
 path='/run/neurogenic-hil/'"$item"'.sha256'
 if test -f "$path" && ! test -L "$path"; then printf '%s_sha256=%s\n' "$item" "$(cat "$path")"
 else printf '%s_sha256=absent\n' "$item"; fi
done
"""

BRIDGE_READY = f"""set -eu
test -f '{RUNTIME}/bridge.ready' && test ! -L '{RUNTIME}/bridge.ready'
test \"$(wc -c < '{RUNTIME}/bridge.ready')\" -le 64
printf 'bridge_ready=1\\n'
"""


def upload_script(size: int, digest: str) -> str:
    if size <= 0 or not re.fullmatch(r"[0-9a-f]{64}", digest):
        raise HilError("invalid artifact metadata")
    return f"""set -eu
umask 077; d='{RUNTIME}'; builds='{BUILDS}'; incoming="$builds/{digest}.incoming"; final="$builds/{digest}.so"
test ! -L "$d"; if test ! -d "$d"; then mkdir "$d"; fi
test ! -L "$builds"; if test ! -d "$builds"; then mkdir "$builds"; fi
if test -f "$final" && test ! -L "$final"; then
 got=$(sha256sum "$final" | cut -d' ' -f1); test "$got" = '{digest}'
 printf 'deployed_sha256=%s\n' "$got"; exit 0
fi
count=0
for path in "$builds"/*.so; do
 test -e "$path" || continue; test -f "$path" && test ! -L "$path"
 count=$((count + 1))
done
test "$count" -lt 3
test ! -e "$incoming" && test ! -L "$incoming"; ( set -C; : > "$incoming" )
cat > "$incoming"; test "$(wc -c < "$incoming")" -eq {size}
got=$(sha256sum "$incoming" | cut -d' ' -f1); test "$got" = '{digest}'; chmod 0700 "$incoming"
test ! -e "$final" && test ! -L "$final"; mv "$incoming" "$final"
printf 'deployed_sha256=%s\n' "$got"
"""


def dropin_text(digest: str) -> str:
    if not re.fullmatch(r"[0-9a-f]{64}", digest):
        raise HilError("invalid activation hash")
    return f"[Service]\nEnvironment=LD_PRELOAD={BUILDS}/{digest}.so\n"


def activation_script(digest: str) -> str:
    text = dropin_text(digest)
    return f"""set -eu
target='{digest}'; library='{BUILDS}/{digest}.so'
test -f "$library" && test ! -L "$library"
test "$(sha256sum "$library" | cut -d' ' -f1)" = "$target"
test "$(systemctl show ac50.service --property=ActiveState --value)" = active
dropins=$(systemctl show ac50.service --property=DropInPaths --value)
case "$dropins" in ''|'{DROPIN}') ;; *) exit 42 ;; esac
mkdir -p '/run/systemd/system/ac50.service.d'; test ! -L '/run/systemd/system/ac50.service.d'
old=''
if test -e '{ACTIVE}' || test -L '{ACTIVE}'; then
 test -f '{ACTIVE}' && test ! -L '{ACTIVE}'; old=$(cat '{ACTIVE}')
 case "$old" in *[!0-9a-f]*|'') exit 43 ;; esac; test "${{#old}}" -eq 64
fi
test "$old" != "$target"
if test -e '{DROPIN}' || test -L '{DROPIN}'; then
 test -f '{DROPIN}' && test ! -L '{DROPIN}'; test -n "$old"
 expected=$(printf '[Service]\nEnvironment=LD_PRELOAD={BUILDS}/%s.so\n' "$old" | sha256sum | cut -d' ' -f1)
 actual=$(sha256sum '{DROPIN}' | cut -d' ' -f1); test "$actual" = "$expected"
fi
pending='{DROPIN}.pending'; test ! -e "$pending" && test ! -L "$pending"
( set -C; cat > "$pending" ) <<'HIL_DROPIN'
{text}HIL_DROPIN
mv "$pending" '{DROPIN}'
systemctl daemon-reload; systemctl restart ac50.service
if test -n "$old"; then printf '%s\n' "$old" > '{PREVIOUS}'; fi
printf '%s\n' "$target" > '{ACTIVE}'
for path in '{BUILDS}'/*.so; do
 test -e "$path" || continue; test -f "$path" && test ! -L "$path"
 name=${{path##*/}}; hash=${{name%.so}}
 if test "$hash" != "$target" && test "$hash" != "$old"; then rm "$path"; fi
done
printf 'activated_sha256=%s\n' "$target"
"""

RESTORE = f"""set -eu
if test -e '{DROPIN}' || test -L '{DROPIN}'; then
 test -f '{DROPIN}' && test ! -L '{DROPIN}'
 actual=$(sha256sum '{DROPIN}' | cut -d' ' -f1); valid=0
 legacy=$(printf '%s' '{LEGACY_DROPIN_TEXT}' | sha256sum | cut -d' ' -f1)
 test "$actual" = "$legacy" && valid=1
 if test -f '{ACTIVE}' && test ! -L '{ACTIVE}'; then
  active=$(cat '{ACTIVE}'); expected=$(printf '[Service]\nEnvironment=LD_PRELOAD={BUILDS}/%s.so\n' "$active" | sha256sum | cut -d' ' -f1)
  test "$actual" = "$expected" && valid=1
 fi
 if test -d '{BUILDS}' && test ! -L '{BUILDS}'; then
  for library in '{BUILDS}'/*.so; do
   test -e "$library" || continue; test -f "$library" && test ! -L "$library"
   name=${{library##*/}}; hash=${{name%.so}}
   case "$hash" in *[!0-9a-f]*|'') exit 44 ;; esac; test "${{#hash}}" -eq 64
   expected=$(printf '[Service]\nEnvironment=LD_PRELOAD={BUILDS}/%s.so\n' "$hash" | sha256sum | cut -d' ' -f1)
   test "$actual" = "$expected" && valid=1
  done
 fi
 test "$valid" -eq 1; rm '{DROPIN}'
fi
systemctl daemon-reload; systemctl restart ac50.service
for path in '{RUNTIME}/bridge.ready' '{RUNTIME}/request' '{RUNTIME}/request.pending' \
 '{RUNTIME}/response' '{RUNTIME}/response.pending' \
 '{ACTIVE}' '{PREVIOUS}' '{RUNTIME}/current.so' '{RUNTIME}/previous.so'; do
 if test -e "$path" || test -L "$path"; then test -f "$path" && test ! -L "$path"; rm "$path"; fi
done
if test -e '{RUNTIME}/request.lock' || test -L '{RUNTIME}/request.lock'; then
 test -d '{RUNTIME}/request.lock' && test ! -L '{RUNTIME}/request.lock'
 rmdir '{RUNTIME}/request.lock'
fi
if test -d '{BUILDS}' && test ! -L '{BUILDS}'; then
 for path in '{BUILDS}'/*; do
  test -e "$path" || continue; test -f "$path" && test ! -L "$path"
  name=${{path##*/}}
  case "$name" in
   ????????????????????????????????????????????????????????????????.so|\
   ????????????????????????????????????????????????????????????????.incoming) ;;
   *) exit 44 ;;
  esac
  hash=${{name%%.*}}; case "$hash" in *[!0-9a-f]*|'') exit 44 ;; esac
  rm "$path"
 done
 rmdir '{BUILDS}'
fi
rmdir '{RUNTIME}'
printf 'stock_restart_requested=1\\n'
"""


class Ssh:
    """No discovery and no arbitrary remote-command API."""

    def __init__(self, config: Config, runner: Callable[..., subprocess.CompletedProcess] = subprocess.run):
        self.config, self.runner = config, runner

    def _args(self, script: str) -> list[str]:
        return [
            "ssh", "-F", "/dev/null", "-p", str(PORT), "-i", str(self.config.identity),
            "-o", "BatchMode=yes", "-o", "PasswordAuthentication=no",
            "-o", "KbdInteractiveAuthentication=no", "-o", "PubkeyAuthentication=yes",
            "-o", "IdentitiesOnly=yes",
            "-o", "StrictHostKeyChecking=yes", "-o", f"UserKnownHostsFile={self.config.known_hosts}",
            "-o", "GlobalKnownHostsFile=/dev/null", "-o", "ConnectTimeout=5",
            "-o", "ServerAliveInterval=2", "-o", "ServerAliveCountMax=2",
            f"{USER}@{HOST}", script,
        ]

    def _call(self, script: str, payload: bytes | None = None, timeout: float = 10) -> bytes:
        environment = {"PATH": os.environ.get("PATH", os.defpath), "LC_ALL": "C"}
        for name in ("HOME", "SSH_AUTH_SOCK"):
            if name in os.environ:
                environment[name] = os.environ[name]
        try:
            result = self.runner(
                self._args(script), input=payload, capture_output=True, check=False,
                timeout=timeout, env=environment,
            )
        except (OSError, subprocess.TimeoutExpired) as exc:
            raise TransportError(f"SSH operation failed: {exc}") from exc
        if len(result.stdout) > MAX_OUTPUT or len(result.stderr) > MAX_OUTPUT:
            raise TransportError("SSH output exceeded its bound")
        if result.returncode:
            detail = result.stderr.decode("ascii", "replace").strip()[:512]
            raise RemoteError(f"remote operation exited {result.returncode}: {detail}")
        return result.stdout

    def fixed(self, name: str, timeout: float = 10) -> bytes:
        scripts = {
            "identity": IDENTITY,
            "collect": COLLECT,
            "bridge": BRIDGE_READY,
            "restore": RESTORE,
        }
        if name not in scripts:
            raise HilError("unknown fixed remote operation")
        return self._call(scripts[name], timeout=timeout)

    def upload(self, data: bytes, digest: str) -> bytes:
        return self._call(upload_script(len(data), digest), data, 15)

    def activate(self, digest: str) -> bytes:
        return self._call(activation_script(digest), timeout=15)

    def test_command(self, command: str) -> bytes:
        if len(command) > 64 or not re.fullmatch(r"[A-Z0-9 +\-]+", command):
            raise HilError("invalid bounded HIL command")
        encoded_size = len(command.encode("ascii")) + 1
        script = f"""set -eu
d='{RUNTIME}'; request="$d/request"; response="$d/response"; pending="$d/request.pending"; lock="$d/request.lock"
test -f "$d/bridge.ready" && test ! -L "$d/bridge.ready"
test ! -e "$request" && test ! -L "$request"
test ! -e "$response" && test ! -L "$response"
test ! -e "$pending" && test ! -L "$pending"
test ! -e "$lock" && test ! -L "$lock"; mkdir "$lock"
trap 'rm -f "$pending"; if test -d "$lock" && ! test -L "$lock"; then rmdir "$lock"; fi' 0 1 2 3 15
test ! -e "$request" && test ! -L "$request"
test ! -e "$response" && test ! -L "$response"
test ! -e "$pending" && test ! -L "$pending"
( set -C; printf '%s\n' '{command}' > "$pending" )
test -f "$pending" && test ! -L "$pending"
test "$(wc -c < "$pending")" -eq {encoded_size}; chmod 0600 "$pending"
test ! -e "$request" && test ! -L "$request"
test ! -e "$response" && test ! -L "$response"
mv "$pending" "$request"
i=0
while test "$i" -lt 6; do
 if test -f "$response" && test ! -L "$response"; then
  test "$(wc -c < "$response")" -le 192
  cat "$response"; rm "$response"; exit 0
 fi
 sleep 1; i=$((i + 1))
done
exit 70
"""
        return self._call(script, timeout=8)


def identity(ssh: Ssh, require_active: bool = True) -> dict[str, str]:
    facts = parse_facts(ssh.fixed("identity"))
    required = {
        "schema": "neurogenic-hil-device-v1", "architecture": "aarch64",
        "firmware_prefix_sha256": FIRMWARE_SHA, "service_load": "loaded",
    }
    if any(facts.get(key) != value for key, value in required.items()):
        raise IdentityError("device, architecture, firmware, or service identity mismatch")
    active = (
        facts.get("service_active") == "active" and facts.get("service_sub") == "running"
        and facts.get("main_pid", "").isdigit() and int(facts["main_pid"]) > 0
        and facts.get("main_exe") == "/usr/bin/MPC Sample"
    )
    if require_active and not active:
        raise IdentityError("stock application is not in the expected state")
    return facts


def confirm(value: str | None) -> None:
    if value != CONFIRM:
        raise HilError(f"device-changing command requires --confirm {CONFIRM}")


def read_reviewed_artifact(artifact: Path, sums_file: Path) -> tuple[bytes, str]:
    try:
        data = artifact.read_bytes()
        sums = sums_file.read_bytes()
    except OSError as exc:
        raise HilError(f"reviewed build artifact unavailable: {exc}") from exc
    if (
        not data or len(data) > 16 * 1024 * 1024 or artifact.is_symlink()
        or len(sums) > 4096 or sums_file.is_symlink()
    ):
        raise HilError("artifact or checksum inventory is unsafe")
    try:
        entries = [line.split() for line in sums.decode("ascii").splitlines()]
    except UnicodeDecodeError as exc:
        raise HilError("checksum inventory is not ASCII") from exc
    matches = [fields[0] for fields in entries if len(fields) == 2 and fields[1] == artifact.name]
    digest = hashlib.sha256(data).hexdigest()
    if len(matches) != 1 or not re.fullmatch(r"[0-9a-f]{64}", matches[0]) or matches[0] != digest:
        raise HilError("artifact does not match the reviewed SHA256SUMS inventory")
    return data, digest


def parse_hil_response(raw: bytes) -> dict[str, str]:
    if len(raw) > 192:
        raise RemoteError("HIL response exceeded 192 bytes")
    try:
        text = raw.decode("ascii").strip()
    except UnicodeDecodeError as exc:
        raise RemoteError("HIL response is not ASCII") from exc
    fields: dict[str, str] = {}
    for token in text.split():
        if "=" not in token:
            raise RemoteError("malformed HIL response")
        key, value = token.split("=", 1)
        if key in fields or not re.fullmatch(r"[a-z0-9_]+", key):
            raise RemoteError("ambiguous HIL response")
        fields[key] = value
    if "error" in fields:
        return fields
    required = {
        "v", "open", "track", "step", "bits", "flow",
        "repaint", "restore", "pending_repaint",
    }
    if set(fields) != required or fields["v"] != "1":
        raise RemoteError("incomplete HIL state response")
    return fields


def parse_native_response(raw: bytes) -> dict[str, str]:
    if len(raw) > 192:
        raise RemoteError("native HIL response exceeded 192 bytes")
    try:
        tokens = raw.decode("ascii").strip().split()
    except UnicodeDecodeError as exc:
        raise RemoteError("native HIL response is not ASCII") from exc
    fields: dict[str, str] = {}
    for token in tokens:
        if "=" not in token:
            raise RemoteError("malformed native HIL response")
        key, value = token.split("=", 1)
        if key in fields or not re.fullmatch(r"[a-z_]+", key):
            raise RemoteError("ambiguous native HIL response")
        fields[key] = value
    required = {
        "v", "native", "context", "action_accepted", "command_accepted",
        "event_observed", "event_removed", "paint_after_arm", "diagnostic",
    }
    if set(fields) != required or fields["v"] != "2":
        raise RemoteError("incomplete native HIL response")
    if fields["context"] not in ("ready", "unavailable"):
        raise RemoteError("invalid native context status")
    if any(fields[key] not in ("0", "1") for key in (
        "action_accepted", "command_accepted", "event_observed", "event_removed",
        "paint_after_arm",
    )):
        raise RemoteError("invalid native evidence bit")
    if fields["diagnostic"] not in {
        "not_reached", "waiting_for_paint", "paint_entered",
        "expired_before_paint", "expired_after_paint", "context_unavailable",
        "context_changed", "wrong_thread", "request_rejected", "native_failure",
        "command_accepted", "event_observed", "remove_accepted", "event_removed",
        "ambiguous_event",
    }:
        raise RemoteError("invalid native diagnostic stage")
    if fields["native"] not in {
        "unavailable", "idle", "armed", "accepted", "observed",
        "remove_requested", "remove_accepted", "removed", "rejected",
        "expired", "failed", "inserting", "removing",
    }:
        raise RemoteError("invalid native experiment status")
    return fields


def parse_native_scan_response(raw: bytes) -> dict[str, str]:
    if len(raw) > 192:
        raise RemoteError("native scan response exceeded 192 bytes")
    try:
        tokens = raw.decode("ascii").strip().split()
    except UnicodeDecodeError as exc:
        raise RemoteError("native scan response is not ASCII") from exc
    fields: dict[str, str] = {}
    for token in tokens:
        if "=" not in token:
            raise RemoteError("malformed native scan response")
        key, value = token.split("=", 1)
        if key in fields or not re.fullmatch(r"[a-z0-9_]+", key):
            raise RemoteError("ambiguous native scan response")
        fields[key] = value
    required = {"v", "attempts", "traversed", "tag3", "tick", "note", "native", "bytes"}
    if set(fields) != required or fields["v"] != "1":
        raise RemoteError("incomplete native scan response")
    for key in required - {"v"}:
        if not fields[key].isdigit() or int(fields[key]) > 4096:
            raise RemoteError("invalid native scan counter")
    return fields


def parse_grid_response(raw: bytes) -> dict[str, str]:
    """Parse the bounded, pointer-free M0.5 grid diagnostic snapshot."""
    if len(raw) > 192:
        raise RemoteError("native grid response exceeded 192 bytes")
    try:
        tokens = raw.decode("ascii").strip().split()
    except UnicodeDecodeError as exc:
        raise RemoteError("native grid response is not ASCII") from exc
    fields: dict[str, str] = {}
    for token in tokens:
        if "=" not in token:
            raise RemoteError("malformed native grid response")
        key, value = token.split("=", 1)
        if key in fields or not re.fullmatch(r"[a-z0-9_]+", key):
            raise RemoteError("ambiguous native grid response")
        fields[key] = value
    required = {
        "v", "hook", "paint", "ctx", "draw", "open", "input", "intent",
        "ins", "obs", "rm", "inv", "state",
    }
    if set(fields) != required or fields["v"] != "2":
        raise RemoteError("incomplete native grid response")
    if any(fields[key] not in ("0", "1") for key in ("hook", "ctx", "open")):
        raise RemoteError("invalid native grid evidence bit")
    for key in required - {"v", "hook", "ctx", "open", "state"}:
        if not fields[key].isdigit() or int(fields[key]) > 2**32 - 1:
            raise RemoteError("invalid native grid counter")
    if fields["state"] not in {
        "unavailable", "inactive", "ready", "pending", "failed",
        "context_changed", "busy",
    }:
        raise RemoteError("invalid native grid state")
    return fields


def removal_command_accepted(state: dict[str, str]) -> bool:
    """Accept a polled state at or beyond removal dispatch, with sticky proof."""
    if state["native"] == "remove_accepted":
        return state["event_removed"] == "0" and state["diagnostic"] == "remove_accepted"
    return (
        state["native"] == "removed"
        and state["event_removed"] == "1"
        and state["diagnostic"] == "event_removed"
    )


def execute_level_b_scenario(
    ssh: Ssh, records: list[dict[str, object]] | None = None
) -> list[dict[str, object]]:
    steps = [
        ("RESET", {"open": "0", "track": "1", "step": "1", "bits": "0000"}),
        ("OPEN", {"open": "1"}),
        ("SELECT 4", {"track": "4"}),
        ("MOVE 3", {"step": "4"}),
        ("TOGGLE", {"bits": "0008"}),
        ("QUERY", {"open": "1", "track": "4", "step": "4", "bits": "0008"}),
        ("CLOSE", {"open": "0", "restore": "1"}),
        ("RESET", {"open": "0", "track": "1", "step": "1", "bits": "0000"}),
    ]
    if records is None:
        records = []
    for command, expected in steps:
        started = time.monotonic()
        try:
            actual = parse_hil_response(ssh.test_command(command))
        except HilError as exc:
            records.append({
                "level": "B", "command": command, "expected": expected,
                "actual": None, "pass": False, "error": str(exc),
                "duration_ms": round((time.monotonic() - started) * 1000, 3),
            })
            raise
        passed = all(actual.get(key) == value for key, value in expected.items())
        records.append({
            "level": "B", "command": command, "expected": expected,
            "actual": actual, "pass": passed,
            "duration_ms": round((time.monotonic() - started) * 1000, 3),
        })
    return records


def print_assertion_failures(records: list[dict[str, object]]) -> None:
    """Print compact diagnostics while report.json remains authoritative."""
    failures = [record for record in records if not record.get("pass", False)]
    if not failures:
        return
    print("Level B assertion failures:")
    for record in failures:
        print(f"- Command: {record.get('command', 'unknown')}")
        print(f"  Expected: {json.dumps(record.get('expected'), sort_keys=True)}")
        print(f"  Actual: {json.dumps(record.get('actual'), sort_keys=True)}")
        if record.get("error"):
            print(f"  Error: {record['error']}")
    first = failures[0]
    print(
        "First unexpected state transition: "
        f"{first.get('command', 'unknown')} expected "
        f"{json.dumps(first.get('expected'), sort_keys=True)}, actual "
        f"{json.dumps(first.get('actual'), sort_keys=True)}"
    )


def run_level_b_scenario(ssh: Ssh) -> list[dict[str, object]]:
    records = execute_level_b_scenario(ssh)
    for record in records:
        print(json.dumps(record, sort_keys=True))
    if any(not record["pass"] for record in records):
        print_assertion_failures(records)
        raise RemoteError("one or more Level B state assertions failed")
    return records


def settle(ssh: Ssh, dropin: str, old_pid: str | None = None) -> dict[str, str]:
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        try:
            facts = identity(ssh)
            if facts.get("dropins") == dropin and (old_pid is None or facts.get("main_pid") != old_pid):
                return facts
        except HilError:
            pass
        time.sleep(0.5)
    raise RemoteError("application did not settle after the single requested restart")


def build_hil_artifact() -> tuple[bytes, str]:
    try:
        code = subprocess.run(
            [str(HIL_BUILD)], cwd=ROOT, check=False, timeout=300
        ).returncode
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise HilError(f"fixed HIL build failed: {exc}") from exc
    if code:
        raise HilError(f"fixed HIL build failed with status {code}")
    return read_reviewed_artifact(HIL_ARTIFACT, HIL_SUMS)


def build_native_artifact() -> tuple[bytes, str]:
    command = [
        "nix", "shell", "nixpkgs#pkgsCross.aarch64-multiplatform.stdenv.cc",
        "nixpkgs#qemu", "nixpkgs#llvmPackages_21.llvm",
        "--command", "zsh", "-lc",
        "set -eu; "
        "export AARCH64_CXX=$(command -v aarch64-unknown-linux-gnu-g++); "
        "device/m04a/scripts/verify-native-abi.sh; device/m04a/scripts/build.sh",
    ]
    try:
        code = subprocess.run(command, cwd=ROOT, check=False, timeout=300).returncode
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise HilError(f"fixed M0.4 native build failed: {exc}") from exc
    if code:
        raise HilError(f"fixed M0.4 native build failed with status {code}")
    return read_reviewed_artifact(M04A_ARTIFACT, M04A_SUMS)


def build_native_grid_artifact() -> tuple[bytes, str]:
    command = [
        "nix", "shell", "nixpkgs#pkgsCross.aarch64-multiplatform.stdenv.cc",
        "nixpkgs#qemu", "nixpkgs#llvmPackages_21.llvm", "--command", "zsh", "-lc",
        "set -eu; "
        "export AARCH64_CXX=$(command -v aarch64-unknown-linux-gnu-g++); "
        "device/m04a/scripts/verify-native-abi.sh; device/m05a/scripts/build.sh",
    ]
    try:
        code = subprocess.run(command, cwd=ROOT, check=False, timeout=300).returncode
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise HilError(f"fixed M0.5a native-grid build failed: {exc}") from exc
    if code:
        raise HilError(f"fixed M0.5a native-grid build failed with status {code}")
    return read_reviewed_artifact(M05A_ARTIFACT, M05A_SUMS)


def deploy_artifact(ssh: Ssh, data: bytes, digest: str) -> None:
    if parse_facts(ssh.upload(data, digest)) != {"deployed_sha256": digest}:
        raise RemoteError("deployment acknowledgement mismatch")


def activate_artifact(ssh: Ssh, digest: str, before: dict[str, str]) -> dict[str, str]:
    if before.get("dropins") not in ("", DROPIN):
        raise IdentityError("unexpected ac50.service drop-in")
    if parse_facts(ssh.activate(digest)) != {"activated_sha256": digest}:
        raise RemoteError("activation acknowledgement mismatch")
    active = settle(ssh, DROPIN, before["main_pid"])
    state = parse_facts(ssh.fixed("collect"))
    if state.get("active_sha256") != digest:
        raise RemoteError("active build state does not match the requested hash")
    return active


def wait_bridge(ssh: Ssh) -> None:
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        try:
            if parse_facts(ssh.fixed("bridge")) == {"bridge_ready": "1"}:
                return
        except HilError:
            pass
        time.sleep(0.25)
    raise RemoteError("HIL bridge did not become ready")


def restore_stock(ssh: Ssh) -> dict[str, str]:
    identity(ssh, False)
    if parse_facts(ssh.fixed("restore", 15)) != {"stock_restart_requested": "1"}:
        raise RemoteError("restore acknowledgement mismatch")
    return settle(ssh, "")


class HostRunLock:
    """Exclusive host-side ownership of one complete HIL cycle."""

    def __init__(self, path: Path | None = None):
        self.path = RUN_LOCK if path is None else path
        self.owned = False

    def __enter__(self) -> "HostRunLock":
        try:
            self.path.parent.mkdir(parents=True, exist_ok=True)
        except OSError as exc:
            raise RunCycleError(f"cannot prepare HIL run lock: {exc}") from exc
        try:
            self.path.mkdir(mode=0o700)
        except FileExistsError as exc:
            raise RunCycleError(
                f"another or incomplete HIL run owns {self.path}; recover it explicitly"
            ) from exc
        self.owned = True
        owner = self.path / "owner.json"
        try:
            owner.write_text(
                json.dumps({"pid": os.getpid()}, sort_keys=True) + "\n",
                encoding="ascii",
            )
        except OSError as exc:
            self.path.rmdir()
            self.owned = False
            raise RunCycleError(f"cannot record HIL run-lock ownership: {exc}") from exc
        return self

    def __exit__(self, _kind, _value, _traceback) -> None:
        if not self.owned:
            return
        owner = self.path / "owner.json"
        if owner.is_file() and not owner.is_symlink():
            owner.unlink()
        self.path.rmdir()
        self.owned = False


def utc_now() -> str:
    return datetime.datetime.now(datetime.timezone.utc).isoformat().replace("+00:00", "Z")


def create_report_directory() -> Path:
    try:
        RUN_ROOT.mkdir(parents=True, exist_ok=True)
    except OSError as exc:
        raise RunCycleError(f"cannot prepare HIL report root: {exc}") from exc
    if RUN_ROOT.is_symlink() or not RUN_ROOT.is_dir():
        raise RunCycleError("unsafe HIL run-report root")
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    path = RUN_ROOT / f"{stamp}-{os.getpid()}"
    try:
        path.mkdir(mode=0o700)
    except OSError as exc:
        raise RunCycleError(f"cannot create unique HIL report directory: {exc}") from exc
    return path


def git_commit() -> str:
    try:
        result = subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, check=False,
            capture_output=True, text=True, timeout=5,
        )
    except (OSError, subprocess.TimeoutExpired):
        return "unknown"
    commit = result.stdout.strip()
    if result.returncode or not re.fullmatch(r"[0-9a-f]{40}", commit):
        return "unknown"
    return commit


def write_run_report(directory: Path, report: dict[str, object]) -> None:
    report_path = directory / "report.json"
    summary_path = directory / "summary.txt"
    if report_path.exists() or summary_path.exists():
        raise RunCycleError("run report already exists")
    encoded = (json.dumps(report, indent=2, sort_keys=True) + "\n").encode("utf-8")
    if len(encoded) > 128 * 1024:
        raise RunCycleError("run report exceeded 128 KiB")
    try:
        report_path.write_bytes(encoded)
    except OSError as exc:
        raise RunCycleError(f"cannot write JSON run report: {exc}") from exc
    assertions = report.get("assertions", {})
    summary = (
        f"overall={report['status']}\n"
        f"level={report['level']}\n"
        f"assertions_passed={assertions.get('passed', 0)}\n"
        f"assertions_failed={assertions.get('failed', 0)}\n"
        f"artifact_sha256={report.get('artifact_sha256') or 'none'}\n"
        f"restoration={report.get('restoration', {}).get('status', 'not-required')}\n"
        f"report_directory={directory}\n"
    )
    try:
        summary_path.write_text(summary, encoding="utf-8")
    except OSError as exc:
        raise RunCycleError(f"cannot write text run summary: {exc}") from exc


def orchestrate_run(args: argparse.Namespace, ssh: Ssh | None) -> int:
    if args.level == "B":
        confirm(args.confirm)
        if not args.scenario:
            raise RunCycleError("Level B run currently requires --scenario")
    with HostRunLock():
        report_dir = create_report_directory()
        report: dict[str, object] = {
            "schema": "neurogenic-hil-run-v1",
            "started_at": utc_now(),
            "git_commit": git_commit(),
            "level": args.level,
            "scenario": bool(args.scenario),
            "status": "running",
            "artifact_sha256": None,
            "firmware_fingerprint": None,
            "initial_state": None,
            "active_state": None,
            "final_state": None,
            "activation": {"status": "not-attempted"},
            "restoration": {"status": "not-required"},
            "tests": [],
            "diagnostics": None,
            "observation_scan": None,
            "removal_scan": None,
            "steps": [],
            "errors": [],
            "assertions": {"passed": 0, "failed": 0},
        }
        original_error: HilError | None = None
        restoration_error: HilError | None = None
        mutated = False

        def step(name: str, operation: Callable[[], object]) -> object:
            started = time.monotonic()
            record: dict[str, object] = {"name": name, "status": "running"}
            report["steps"].append(record)
            try:
                value = operation()
                record["status"] = "pass"
                return value
            except HilError as exc:
                record["status"] = "fail"
                record["error"] = str(exc)
                raise
            finally:
                record["duration_ms"] = round((time.monotonic() - started) * 1000, 3)

        try:
            if args.level == "A":
                print("[1/2] Testing host reducer...", flush=True)
                def level_a() -> None:
                    try:
                        code = subprocess.run(
                            ["cargo", "test", "-p", "neurogenic-device", "--features",
                             "hil-test-interface", "--test", "hil"],
                            cwd=ROOT, check=False, timeout=120,
                        ).returncode
                    except (OSError, subprocess.TimeoutExpired) as exc:
                        raise HilError(f"Level A tests failed: {exc}") from exc
                    if code:
                        raise HilError(f"Level A tests failed with status {code}")
                step("level-a-tests", level_a)
                report["tests"] = [{"level": "A", "suite": "device-hil", "pass": True}]
                report["assertions"] = {"passed": 1, "failed": 0}
            else:
                initial = step("initial-identity", lambda: identity(ssh))
                report["initial_state"] = initial
                report["firmware_fingerprint"] = initial["firmware_prefix_sha256"]
                if initial.get("dropins") != "":
                    raise IdentityError("run requires stock ac50.service with no drop-in")
                prior = step("initial-runtime-state", lambda: parse_facts(ssh.fixed("collect")))
                if (
                    prior.get("hil_runtime") != "absent"
                    or prior.get("active_sha256") != "absent"
                    or prior.get("previous_sha256") != "absent"
                ):
                    raise IdentityError("an earlier HIL runtime session requires explicit recovery")

                print("[1/6] Building...", flush=True)
                data, digest = step("build", build_hil_artifact)
                report["artifact_sha256"] = digest

                print("[2/6] Deploying...", flush=True)
                mutated = True
                step("deploy", lambda: deploy_artifact(ssh, data, digest))

                print("[3/6] Activating...", flush=True)
                report["activation"] = {"status": "running", "sha256": digest}
                try:
                    active = step("activate", lambda: activate_artifact(ssh, digest, initial))
                except HilError as exc:
                    report["activation"] = {
                        "status": "fail", "sha256": digest, "message": str(exc),
                    }
                    raise
                report["active_state"] = active
                report["activation"] = {"status": "pass", "sha256": digest}
                step("bridge-ready", lambda: wait_bridge(ssh))

                print("[4/6] Testing...", flush=True)
                records: list[dict[str, object]] = []
                report["tests"] = records
                step("level-b-tests", lambda: execute_level_b_scenario(ssh, records))
                passed = sum(bool(item["pass"]) for item in records)
                failed = len(records) - passed
                report["assertions"] = {"passed": passed, "failed": failed}
                if failed:
                    raise RemoteError("one or more Level B state assertions failed")
                report["diagnostics"] = step(
                    "collect", lambda: parse_facts(ssh.fixed("collect"))
                )
        except HilError as exc:
            original_error = exc
            test_records = report["tests"]
            if test_records:
                passed = sum(bool(item["pass"]) for item in test_records)
                report["assertions"] = {
                    "passed": passed,
                    "failed": len(test_records) - passed,
                }
                print_assertion_failures(test_records)
            report["errors"].append({"phase": "test-cycle", "message": str(exc)})
            if mutated:
                try:
                    report["diagnostics"] = parse_facts(ssh.fixed("collect"))
                except HilError as diagnostic_error:
                    report["errors"].append({
                        "phase": "diagnostic-collection", "message": str(diagnostic_error),
                    })
        except KeyboardInterrupt:
            original_error = RunCycleError("run interrupted by operator")
            report["errors"].append({
                "phase": "test-cycle", "message": str(original_error),
            })
        finally:
            if mutated:
                print("[5/6] Restoring stock...", flush=True)
                try:
                    final = step("restore-stock", lambda: restore_stock(ssh))
                    report["final_state"] = final
                    report["restoration"] = {"status": "pass"}
                except HilError as exc:
                    restoration_error = exc
                    report["restoration"] = {"status": "fail", "message": str(exc)}
                    report["errors"].append({"phase": "restoration", "message": str(exc)})

            print(
                "[6/6] Reporting..." if args.level == "B" else "[2/2] Reporting...",
                flush=True,
            )
            report["finished_at"] = utc_now()
            report["status"] = "pass" if original_error is None and restoration_error is None else "fail"
            write_run_report(report_dir, report)

        assertions = report["assertions"]
        print(f"Overall {str(report['status']).upper()}")
        print(f"Assertions: {assertions['passed']} passed, {assertions['failed']} failed")
        print(f"Artifact SHA-256: {report['artifact_sha256'] or 'none'}")
        print(f"Restoration: {report['restoration']['status']}")
        print(f"Report directory: {report_dir}")

        if restoration_error is not None:
            raise RunCycleError(
                f"RESTORATION FAILED; MPC MAY STILL BE RUNNING THE TEST MOD. "
                f"Original error: {original_error or 'none'}; restoration: {restoration_error}. "
                "If SSH is unavailable, shut down normally, remove the development SD, "
                "and perform a stock-only boot."
            )
        if original_error is not None:
            raise RunCycleError(
                f"HIL run failed: {original_error}; stock restoration status: "
                f"{report['restoration']['status']}"
            )
        return 0


def orchestrate_native_experiment(args: argparse.Namespace, ssh: Ssh) -> int:
    """Run the single approval-gated native insertion/removal experiment."""
    confirm(args.confirm)
    if args.confirm_native != NATIVE_CONFIRM:
        raise RunCycleError(
            f"native insertion requires --confirm-native {NATIVE_CONFIRM}"
        )
    with HostRunLock():
        report_dir = create_report_directory()
        report: dict[str, object] = {
            "schema": "neurogenic-native-experiment-v1",
            "started_at": utc_now(),
            "git_commit": git_commit(),
            "level": "NATIVE",
            "scenario": "one-a01-tick-zero-add-observe-remove",
            "status": "running",
            "artifact_sha256": None,
            "firmware_fingerprint": None,
            "initial_state": None,
            "active_state": None,
            "final_state": None,
            "activation": {"status": "not-attempted"},
            "restoration": {"status": "not-required"},
            "tests": [],
            "diagnostics": None,
            "steps": [],
            "errors": [],
            "assertions": {"passed": 0, "failed": 0},
            "audible_playback_observed": False,
            "operator_preconditions": {
                "fresh_unsaved_sequence": "required-not-machine-verified",
                "transport_stopped": "required-not-machine-verified",
                "a01_sample_assigned": "required-not-machine-verified",
            },
        }
        original_error: HilError | None = None
        restoration_error: HilError | None = None
        mutated = False
        passed_assertions = 0

        def step(name: str, operation: Callable[[], object]) -> object:
            started = time.monotonic()
            record: dict[str, object] = {"name": name, "status": "running"}
            report["steps"].append(record)
            try:
                value = operation()
                record["status"] = "pass"
                return value
            except HilError as exc:
                record["status"] = "fail"
                record["error"] = str(exc)
                raise
            finally:
                record["duration_ms"] = round(
                    (time.monotonic() - started) * 1000, 3
                )

        def command(name: str) -> dict[str, str]:
            started = time.monotonic()
            actual = parse_native_response(ssh.test_command(name))
            report["tests"].append({
                "level": "native", "command": name, "actual": actual,
                "duration_ms": round((time.monotonic() - started) * 1000, 3),
            })
            return actual

        def scan_command() -> dict[str, str]:
            started = time.monotonic()
            actual = parse_native_scan_response(ssh.test_command("NATIVE DIAG"))
            report["tests"].append({
                "level": "native", "command": "NATIVE DIAG", "actual": actual,
                "duration_ms": round((time.monotonic() - started) * 1000, 3),
            })
            return actual

        def wait_for(
            label: str,
            predicate: Callable[[dict[str, str]], bool],
            timeout: float,
        ) -> dict[str, str]:
            deadline = time.monotonic() + timeout
            while True:
                state = command("NATIVE STATUS")
                if predicate(state):
                    return state
                if state["native"] in {"failed", "rejected", "expired"}:
                    scan = scan_command()
                    raise RemoteError(
                        f"native experiment entered terminal state {state['native']} "
                        f"while waiting for {label}: diagnostic={state['diagnostic']} "
                        f"paint_after_arm={state['paint_after_arm']} scan={scan}"
                    )
                if time.monotonic() >= deadline:
                    scan = scan_command()
                    raise RemoteError(
                        f"native experiment timed out waiting for {label}: "
                        f"state={state['native']} diagnostic={state['diagnostic']} "
                        f"paint_after_arm={state['paint_after_arm']} scan={scan}"
                    )
                time.sleep(0.25)

        try:
            initial = step("initial-identity", lambda: identity(ssh))
            report["initial_state"] = initial
            report["firmware_fingerprint"] = initial["firmware_prefix_sha256"]
            if initial.get("dropins") != "":
                raise IdentityError("native experiment requires stock ac50.service")
            prior = step("initial-runtime-state", lambda: parse_facts(ssh.fixed("collect")))
            if (
                prior.get("hil_runtime") != "absent"
                or prior.get("active_sha256") != "absent"
                or prior.get("previous_sha256") != "absent"
            ):
                raise IdentityError("an earlier HIL session requires explicit recovery")

            print("[1/7] Building and ABI-testing native artifact...", flush=True)
            data, digest = step("build-native", build_native_artifact)
            report["artifact_sha256"] = digest

            predeploy = step("pre-deploy-identity", lambda: identity(ssh))
            if (
                predeploy["firmware_prefix_sha256"]
                != report["firmware_fingerprint"]
                or predeploy.get("dropins") != ""
            ):
                raise IdentityError("device identity changed before native deployment")

            print("[2/7] Deploying immutable native artifact...", flush=True)
            mutated = True
            step("deploy", lambda: deploy_artifact(ssh, data, digest))

            print("[3/7] Activating once...", flush=True)
            report["activation"] = {"status": "running", "sha256": digest}
            active = step("activate", lambda: activate_artifact(ssh, digest, initial))
            report["active_state"] = active
            report["activation"] = {"status": "pass", "sha256": digest}
            step("bridge-ready", lambda: wait_bridge(ssh))

            print(
                "[4/7] Create a fresh unsaved stopped one-bar sequence, assign "
                "A01, and open the stock Step editor; waiting up to 180 seconds "
                "for verified context...",
                flush=True,
            )
            ready = step(
                "step-context-ready",
                lambda: wait_for(
                    "idle Step-editor context",
                    lambda state: state["native"] == "idle"
                    and state["context"] == "ready",
                    180,
                ),
            )
            if ready["command_accepted"] != "0" or ready["event_observed"] != "0":
                raise RemoteError("native experiment was not pristine before arming")
            passed_assertions += 1

            print(
                "[5/7] Prepare to turn DATA one detent when ARM is confirmed; "
                "this requests the stock Step editor's UI-thread repaint.",
                flush=True,
            )
            armed = step("arm-once", lambda: command("NATIVE ARM"))
            if armed["action_accepted"] != "1":
                raise RemoteError("native arm request was rejected")
            passed_assertions += 1
            print("      ARM confirmed: turn DATA one detent now.", flush=True)
            step(
                "command-accepted",
                lambda: wait_for(
                    "stock command acceptance",
                    lambda state: state["command_accepted"] == "1",
                    15,
                ),
            )
            passed_assertions += 1
            print(
                "      Command accepted: turn DATA one detent to verify unique native event identity.",
                flush=True,
            )
            observed = step(
                "event-observed",
                lambda: wait_for(
                    "unique native event-list presence",
                    lambda state: state["event_observed"] == "1"
                    and state["native"] == "observed",
                    10,
                ),
            )
            if observed["event_removed"] != "0":
                raise RemoteError("event reported removed before removal request")
            report["observation_scan"] = scan_command()
            passed_assertions += 1

            print("[6/7] Removing and verifying absence...", flush=True)
            print(
                "      Prepare to turn DATA one detent again when REMOVE is confirmed.",
                flush=True,
            )
            removal = step("remove-once", lambda: command("NATIVE REMOVE"))
            if removal["action_accepted"] != "1":
                raise RemoteError("native removal request was rejected")
            print("      REMOVE armed: turn DATA one detent now.", flush=True)
            step(
                "remove-command-accepted",
                lambda: wait_for(
                    "stock removal-command acceptance",
                    removal_command_accepted,
                    15,
                ),
            )
            passed_assertions += 1
            print(
                "      Removal accepted: turn DATA one detent to verify exact absence.",
                flush=True,
            )
            step(
                "event-removed",
                lambda: wait_for(
                    "native event-list absence",
                    lambda state: state["event_removed"] == "1"
                    and state["native"] == "removed",
                    10,
                ),
            )
            report["removal_scan"] = scan_command()
            passed_assertions += 1
            report["assertions"] = {"passed": passed_assertions, "failed": 0}
            report["diagnostics"] = step(
                "collect", lambda: parse_facts(ssh.fixed("collect"))
            )
        except HilError as exc:
            original_error = exc
            report["assertions"] = {"passed": passed_assertions, "failed": 1}
            report["errors"].append({"phase": "native-experiment", "message": str(exc)})
            if mutated:
                try:
                    report["diagnostics"] = parse_facts(ssh.fixed("collect"))
                except HilError as diagnostic_error:
                    report["errors"].append({
                        "phase": "diagnostic-collection",
                        "message": str(diagnostic_error),
                    })
        except KeyboardInterrupt:
            original_error = RunCycleError("native experiment interrupted by operator")
            report["assertions"] = {"passed": 0, "failed": 1}
            report["errors"].append({
                "phase": "native-experiment", "message": str(original_error),
            })
        finally:
            if mutated:
                print("[7/7] Restoring stock...", flush=True)
                try:
                    final = step("restore-stock", lambda: restore_stock(ssh))
                    report["final_state"] = final
                    report["restoration"] = {"status": "pass"}
                except HilError as exc:
                    restoration_error = exc
                    report["restoration"] = {"status": "fail", "message": str(exc)}
                    report["errors"].append({"phase": "restoration", "message": str(exc)})
            report["finished_at"] = utc_now()
            report["status"] = (
                "pass" if original_error is None and restoration_error is None else "fail"
            )
            write_run_report(report_dir, report)

        print(f"Overall {str(report['status']).upper()}")
        print(f"Artifact SHA-256: {report['artifact_sha256'] or 'none'}")
        print(f"Restoration: {report['restoration']['status']}")
        print(f"Report directory: {report_dir}")
        print("Audible playback: not tested by this experiment")
        if restoration_error is not None:
            raise RunCycleError(
                "RESTORATION FAILED; MPC MAY STILL BE RUNNING THE NATIVE TEST MOD. "
                f"Original error: {original_error or 'none'}; restoration: "
                f"{restoration_error}. If SSH is unavailable, shut down normally, "
                "remove the development SD, and perform a stock-only boot."
            )
        if original_error is not None:
            raise RunCycleError(
                f"native experiment failed: {original_error}; stock restoration "
                f"status: {report['restoration']['status']}"
            )
        return 0


def orchestrate_grid_experiment(
    args: argparse.Namespace,
    ssh: Ssh,
    input_fn: Callable[[str], str] = input,
) -> int:
    """Run one owner-observed M0.5 grid experiment and always restore stock."""
    confirm(args.confirm)
    if args.confirm_grid != GRID_CONFIRM:
        raise RunCycleError(f"playable grid requires --confirm-grid {GRID_CONFIRM}")
    with HostRunLock():
        report_dir = create_report_directory()
        report: dict[str, object] = {
            "schema": "neurogenic-playable-grid-experiment-v1",
            "started_at": utc_now(), "git_commit": git_commit(), "level": "GRID",
            "scenario": "a01-steps-1-5-9-13-remove-9", "status": "running",
            "artifact_sha256": None, "firmware_fingerprint": None,
            "initial_state": None, "active_state": None, "final_state": None,
            "activation": {"status": "not-attempted"},
            "restoration": {"status": "not-required"}, "steps": [],
            "diagnostics": {}, "errors": [],
            "assertions": {"passed": 0, "failed": 0},
            "owner_result": "not-recorded", "audible_playback_observed": False,
            "expected_ticks": [0, 960, 1920, 2880],
        }
        original_error: HilError | None = None
        restoration_error: HilError | None = None
        mutated = False

        def step(name: str, operation: Callable[[], object]) -> object:
            started = time.monotonic()
            record: dict[str, object] = {"name": name, "status": "running"}
            report["steps"].append(record)
            try:
                value = operation()
                record["status"] = "pass"
                return value
            except HilError as exc:
                record.update(status="fail", error=str(exc))
                raise
            finally:
                record["duration_ms"] = round((time.monotonic() - started) * 1000, 3)

        def grid_query() -> dict[str, str]:
            return parse_grid_response(ssh.test_command("GRID QUERY"))

        try:
            initial = step("initial-identity", lambda: identity(ssh))
            report["initial_state"] = initial
            report["firmware_fingerprint"] = initial["firmware_prefix_sha256"]
            if initial.get("dropins") != "":
                raise IdentityError("grid experiment requires stock ac50.service")
            prior = step("initial-runtime-state", lambda: parse_facts(ssh.fixed("collect")))
            if any(prior.get(key) != "absent" for key in (
                "hil_runtime", "active_sha256", "previous_sha256"
            )):
                raise IdentityError("an earlier HIL session requires explicit recovery")

            print("[1/6] Building and inspecting M0.5 grid artifact...", flush=True)
            data, digest = step("build-native-grid", build_native_grid_artifact)
            report["artifact_sha256"] = digest
            check = step("pre-deploy-identity", lambda: identity(ssh))
            if check["firmware_prefix_sha256"] != report["firmware_fingerprint"] or check.get("dropins") != "":
                raise IdentityError("device identity changed before grid deployment")

            print("[2/6] Deploying immutable artifact...", flush=True)
            mutated = True
            step("deploy", lambda: deploy_artifact(ssh, data, digest))
            print("[3/6] Activating once...", flush=True)
            report["activation"] = {"status": "running", "sha256": digest}
            report["active_state"] = step(
                "activate", lambda: activate_artifact(ssh, digest, initial)
            )
            report["activation"] = {"status": "pass", "sha256": digest}
            step("bridge-ready", lambda: wait_bridge(ssh))
            before = step("grid-diagnostic-before", grid_query)
            report["diagnostics"] = {"before": before}
            if before["hook"] != "1":
                raise RemoteError("Step Edit hook did not install")

            print("[4/6] OWNER PHYSICAL TEST (experimental build remains active)", flush=True)
            print(
                "  Use a disposable empty one-bar 4/4 sequence with A01 assigned.\n"
                "  Open Step Edit; NEUROGENIC must appear automatically. Select A01.\n"
                "  Enable steps 1, 5, 9, 13; play and verify four even hits.\n"
                "  Disable step 9; verify three hits. SHIFT+DATA shows stock view;\n"
                "  inspect ticks 0,960,2880 present and tick 1920 absent.",
                flush=True,
            )
            answer = input_fn("Type PASS or FAIL once the single test is complete: ").strip().upper()
            if answer not in {"PASS", "FAIL"}:
                raise RunCycleError("physical acknowledgement must be exactly PASS or FAIL")
            report["owner_result"] = answer.lower()
            report["audible_playback_observed"] = answer == "PASS"
            after = step("grid-diagnostic-after", grid_query)
            report["diagnostics"] = {"before": before, "after": after}
            report["assertions"] = {
                "passed": 1 if answer == "PASS" else 0,
                "failed": 0 if answer == "PASS" else 1,
            }
            if answer == "FAIL":
                raise RunCycleError("owner reported playable-grid acceptance failure")
            required_counters = {
                "paint": 1, "draw": 1, "input": 1, "intent": 5,
                "ins": 4, "obs": 4, "rm": 1,
            }
            missing = {
                key: {"expected_at_least": minimum, "actual": int(after[key])}
                for key, minimum in required_counters.items()
                if int(after[key]) < minimum
            }
            if after["ctx"] != "1" or after["hook"] != "1" or missing:
                raise RemoteError(
                    "owner reported PASS but bounded grid evidence was incomplete: "
                    f"context={after['ctx']} hook={after['hook']} counters={missing}"
                )
        except (EOFError, KeyboardInterrupt):
            original_error = RunCycleError("grid experiment interrupted before acknowledgement")
            report["errors"].append({"phase": "grid-experiment", "message": str(original_error)})
            report["assertions"] = {"passed": 0, "failed": 1}
        except HilError as exc:
            original_error = exc
            report["errors"].append({"phase": "grid-experiment", "message": str(exc)})
            if report["assertions"].get("failed", 0) == 0:
                report["assertions"] = {"passed": 0, "failed": 1}
            diagnostics = report.get("diagnostics")
            if mutated and (not isinstance(diagnostics, dict) or "after" not in diagnostics):
                try:
                    report.setdefault("diagnostics", {})["failure"] = grid_query()
                except HilError as diagnostic_error:
                    report["errors"].append({"phase": "diagnostics", "message": str(diagnostic_error)})
        finally:
            if mutated:
                print("[5/6] Restoring stock...", flush=True)
                try:
                    report["final_state"] = step("restore-stock", lambda: restore_stock(ssh))
                    report["restoration"] = {"status": "pass"}
                except HilError as exc:
                    restoration_error = exc
                    report["restoration"] = {"status": "fail", "message": str(exc)}
                    report["errors"].append({"phase": "restoration", "message": str(exc)})
            report["finished_at"] = utc_now()
            report["status"] = "pass" if original_error is None and restoration_error is None else "fail"
            write_run_report(report_dir, report)

        print("[6/6] Reporting...", flush=True)
        print(f"Overall {str(report['status']).upper()}")
        print(f"Owner acceptance: {report['owner_result']}")
        print(f"Artifact SHA-256: {report['artifact_sha256'] or 'none'}")
        print(f"Restoration: {report['restoration']['status']}")
        print(f"Report directory: {report_dir}")
        if restoration_error is not None:
            raise RunCycleError(
                "RESTORATION FAILED; MPC MAY STILL BE RUNNING THE GRID MOD. "
                f"Original error: {original_error or 'none'}; restoration: {restoration_error}. "
                "If SSH is unavailable, shut down normally, remove the development SD, "
                "and perform a stock-only boot."
            )
        if original_error is not None:
            raise RunCycleError(
                f"grid experiment failed: {original_error}; stock restoration status: "
                f"{report['restoration']['status']}"
            )
        return 0


def run_command(args: argparse.Namespace, ssh: Ssh | None) -> int:
    if args.command == "build":
        if args.native_m05a:
            build_native_grid_artifact()
        elif args.native_m04a:
            build_native_artifact()
        elif args.hil:
            build_hil_artifact()
        else:
            code = subprocess.run([str(BUILD)], cwd=ROOT, check=False).returncode
            if code:
                raise HilError(f"fixed M0.3e build failed with status {code}")
    elif args.command == "test":
        if args.level == "B":
            confirm(args.confirm); identity(ssh)
            if args.scenario:
                run_level_b_scenario(ssh)
            else:
                print(json.dumps(parse_hil_response(ssh.test_command(args.hil_command)), sort_keys=True))
        else:
            code = subprocess.run(
                ["cargo", "test", "-p", "neurogenic-device", "--features",
                 "hil-test-interface", "--test", "hil"], cwd=ROOT, check=False
            ).returncode
            if code:
                raise HilError(f"Level A tests failed with status {code}")
    elif args.command == "status":
        print(json.dumps(identity(ssh), indent=2, sort_keys=True))
    elif args.command == "deploy":
        confirm(args.confirm); identity(ssh)
        if args.native_m05a:
            artifact, sums = M05A_ARTIFACT, M05A_SUMS
        elif args.native_m04a:
            artifact, sums = M04A_ARTIFACT, M04A_SUMS
        elif args.hil:
            artifact, sums = HIL_ARTIFACT, HIL_SUMS
        else:
            artifact, sums = NORMAL_ARTIFACT, NORMAL_SUMS
        data, digest = read_reviewed_artifact(artifact, sums)
        deploy_artifact(ssh, data, digest)
        print(f"staged immutable build {digest}; activate with restart --sha256 {digest}")
    elif args.command == "restart":
        confirm(args.confirm); before = identity(ssh)
        activate_artifact(ssh, args.sha256, before)
        print(f"activated {args.sha256}; ac50.service restarted exactly once and settled")
    elif args.command == "rollback":
        confirm(args.confirm); before = identity(ssh)
        state = parse_facts(ssh.fixed("collect"))
        target = state.get("previous_sha256", "")
        if not re.fullmatch(r"[0-9a-f]{64}", target):
            raise RemoteError("no verified previous build is available")
        if parse_facts(ssh.activate(target)) != {"activated_sha256": target}:
            raise RemoteError("rollback acknowledgement mismatch")
        settle(ssh, DROPIN, before["main_pid"])
        print(f"rolled back to {target}; ac50.service restarted exactly once")
    elif args.command == "restore-stock":
        confirm(args.confirm); restore_stock(ssh)
        print("stock ac50.service restored; volatile HIL files removed")
    elif args.command == "run":
        return orchestrate_run(args, ssh)
    elif args.command == "native-experiment":
        return orchestrate_native_experiment(args, ssh)
    elif args.command == "grid-experiment":
        return orchestrate_grid_experiment(args, ssh)
    elif args.command == "collect":
        identity(ssh, False)
        encoded = (json.dumps(parse_facts(ssh.fixed("collect")), indent=2, sort_keys=True) + "\n").encode()
        if args.output:
            output, root = Path(args.output).resolve(), (ROOT / "target/hil").resolve()
            if output.parent != root or output.exists() or output.is_symlink():
                raise HilError("output must be a new file directly under target/hil")
            root.mkdir(parents=True, exist_ok=True); output.write_bytes(encoded)
        else:
            sys.stdout.buffer.write(encoded)
    return 0


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(prog="neurogenic-hil")
    result.add_argument("--config", type=Path)
    sub = result.add_subparsers(dest="command", required=True)
    build = sub.add_parser("build")
    build_mode = build.add_mutually_exclusive_group()
    build_mode.add_argument("--hil", action="store_true")
    build_mode.add_argument("--native-m04a", action="store_true")
    build_mode.add_argument("--native-m05a", action="store_true")
    sub.add_parser("status")
    deploy = sub.add_parser("deploy")
    deploy_mode = deploy.add_mutually_exclusive_group()
    deploy_mode.add_argument("--hil", action="store_true")
    deploy_mode.add_argument("--native-m04a", action="store_true")
    deploy_mode.add_argument("--native-m05a", action="store_true")
    deploy.add_argument("--confirm")
    restart = sub.add_parser("restart"); restart.add_argument("--sha256", required=True); restart.add_argument("--confirm")
    rollback = sub.add_parser("rollback"); rollback.add_argument("--confirm")
    restore = sub.add_parser("restore-stock"); restore.add_argument("--confirm")
    test = sub.add_parser("test")
    test.add_argument("--level", choices=("A", "B"), default="A")
    test.add_argument("--command", dest="hil_command", default="QUERY")
    test.add_argument("--scenario", action="store_true")
    test.add_argument("--confirm")
    run = sub.add_parser("run")
    run.add_argument("--level", choices=("A", "B"), default="B")
    run.add_argument("--scenario", action="store_true")
    run.add_argument("--confirm")
    native = sub.add_parser("native-experiment")
    native.add_argument("--confirm")
    native.add_argument("--confirm-native")
    grid = sub.add_parser("grid-experiment")
    grid.add_argument("--confirm")
    grid.add_argument("--confirm-grid")
    collect = sub.add_parser("collect"); collect.add_argument("--output")
    return result


def main(argv: Sequence[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        local = (
            args.command == "build"
            or (args.command == "test" and args.level == "A")
            or (args.command == "run" and args.level == "A")
        )
        if not local and args.config is None:
            raise HilError("this command requires --config")
        ssh = None if local else Ssh(Config.load(args.config))
        return run_command(args, ssh)
    except HilError as exc:
        print(f"error: {exc}", file=sys.stderr)
        if args.command in ("restart", "rollback", "run", "native-experiment", "grid-experiment"):
            print(
                f"recovery (explicit, never automatic): rerun with restore-stock --confirm {CONFIRM}",
                file=sys.stderr,
            )
        return exc.exit_code


if __name__ == "__main__":
    raise SystemExit(main())
