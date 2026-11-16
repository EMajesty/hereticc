#!/usr/bin/env python3
"""Strict, bounded host controller for the MPC Sample USB development path."""

from __future__ import annotations
import argparse
import dataclasses
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
FIRMWARE_SHA = "44507c7a7d3029c411b2e3780ea286ca3e08b7e3ffd7b5047d52f73bf5299d1f"
ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "device/m03e/scripts/build-package.sh"
ARTIFACT = ROOT / "target/m03e/build/neurogenic-m03e-ui.so"
ARTIFACT_SUMS = ROOT / "target/m03e/build/SHA256SUMS"
RUNTIME = "/run/neurogenic-hil"
CURRENT, PREVIOUS = RUNTIME + "/current.so", RUNTIME + "/previous.so"
DROPIN = "/run/systemd/system/ac50.service.d/neurogenic-hil.conf"
DROPIN_TEXT = "[Service]\nEnvironment=LD_PRELOAD=/run/neurogenic-hil/current.so\n"
MAX_OUTPUT = 8192


class HilError(Exception):
    exit_code = 2


class Unsupported(HilError):
    exit_code = 3


class IdentityError(HilError):
    exit_code = 4


class TransportError(HilError):
    exit_code = 5


class RemoteError(HilError):
    exit_code = 6


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
prefix=$(head -c 4096 "$stock" | sha256sum | cut -d' ' -f1)
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
for item in current previous; do
 path='/run/neurogenic-hil/'"$item"'.so'
 if test -f "$path" && ! test -L "$path"; then
  printf '%s_sha256=%s\n' "$item" "$(sha256sum "$path" | cut -d' ' -f1)"
 else printf '%s_sha256=absent\n' "$item"; fi
done
"""


def upload_script(size: int, digest: str) -> str:
    if size <= 0 or not re.fullmatch(r"[0-9a-f]{64}", digest):
        raise HilError("invalid artifact metadata")
    return f"""set -eu
umask 077; d='{RUNTIME}'; incoming="$d/incoming.so"
test ! -L "$d"; if test ! -d "$d"; then mkdir "$d"; fi
test ! -e "$incoming" && test ! -L "$incoming"; ( set -C; : > "$incoming" )
cat > "$incoming"; test "$(wc -c < "$incoming")" -eq {size}
got=$(sha256sum "$incoming" | cut -d' ' -f1); test "$got" = '{digest}'; chmod 0700 "$incoming"
if test -e '{CURRENT}' || test -L '{CURRENT}'; then
 test -f '{CURRENT}' && test ! -L '{CURRENT}'
 test ! -e '{PREVIOUS}' && test ! -L '{PREVIOUS}'; mv '{CURRENT}' '{PREVIOUS}'
fi
mv "$incoming" '{CURRENT}'; printf 'deployed_sha256=%s\\n' "$got"
"""


RESTART = f"""set -eu
test -f '{CURRENT}' && test ! -L '{CURRENT}'
test "$(systemctl show ac50.service --property=ActiveState --value)" = active
dropins=$(systemctl show ac50.service --property=DropInPaths --value)
case "$dropins" in ''|'{DROPIN}') ;; *) exit 42 ;; esac
mkdir -p '/run/systemd/system/ac50.service.d'; test ! -L '/run/systemd/system/ac50.service.d'
test ! -e '{DROPIN}' && test ! -L '{DROPIN}'
( set -C; cat > '{DROPIN}' ) <<'HIL_DROPIN'
{DROPIN_TEXT}HIL_DROPIN
systemctl daemon-reload; systemctl restart ac50.service
printf 'restart_requested=1\\n'
"""

RESTORE = f"""set -eu
if test -e '{DROPIN}' || test -L '{DROPIN}'; then
 test -f '{DROPIN}' && test ! -L '{DROPIN}'
 expected=$(printf '%s' '{DROPIN_TEXT}' | sha256sum | cut -d' ' -f1)
 actual=$(sha256sum '{DROPIN}' | cut -d' ' -f1); test "$actual" = "$expected"; rm '{DROPIN}'
fi
systemctl daemon-reload; systemctl restart ac50.service
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
        try:
            result = self.runner(
                self._args(script), input=payload, capture_output=True, check=False,
                timeout=timeout, env={"PATH": os.defpath, "LC_ALL": "C"},
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
        scripts = {"identity": IDENTITY, "collect": COLLECT, "restart": RESTART, "restore": RESTORE}
        if name not in scripts:
            raise HilError("unknown fixed remote operation")
        return self._call(scripts[name], timeout=timeout)

    def upload(self, data: bytes, digest: str) -> bytes:
        return self._call(upload_script(len(data), digest), data, 15)


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


def read_reviewed_artifact() -> tuple[bytes, str]:
    try:
        data = ARTIFACT.read_bytes()
        sums = ARTIFACT_SUMS.read_bytes()
    except OSError as exc:
        raise HilError(f"reviewed build artifact unavailable: {exc}") from exc
    if (
        not data or len(data) > 16 * 1024 * 1024 or ARTIFACT.is_symlink()
        or len(sums) > 4096 or ARTIFACT_SUMS.is_symlink()
    ):
        raise HilError("artifact or checksum inventory is unsafe")
    try:
        entries = [line.split() for line in sums.decode("ascii").splitlines()]
    except UnicodeDecodeError as exc:
        raise HilError("checksum inventory is not ASCII") from exc
    matches = [fields[0] for fields in entries if len(fields) == 2 and fields[1] == ARTIFACT.name]
    digest = hashlib.sha256(data).hexdigest()
    if len(matches) != 1 or not re.fullmatch(r"[0-9a-f]{64}", matches[0]) or matches[0] != digest:
        raise HilError("artifact does not match the reviewed SHA256SUMS inventory")
    return data, digest


def settle(ssh: Ssh, dropin: str, old_pid: str | None = None) -> None:
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        try:
            facts = identity(ssh)
            if facts.get("dropins") == dropin and (old_pid is None or facts.get("main_pid") != old_pid):
                return
        except HilError:
            pass
        time.sleep(0.5)
    raise RemoteError("application did not settle after the single requested restart")


def run_command(args: argparse.Namespace, ssh: Ssh | None) -> int:
    if args.command == "build":
        code = subprocess.run([str(BUILD)], cwd=ROOT, check=False).returncode
        if code:
            raise HilError(f"fixed M0.3e build failed with status {code}")
    elif args.command == "test":
        if args.level == "B":
            raise Unsupported("Level B has a bounded reducer protocol but no reviewed in-process delivery bridge")
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
        data, digest = read_reviewed_artifact()
        if parse_facts(ssh.upload(data, digest)) != {"deployed_sha256": digest}:
            raise RemoteError("deployment acknowledgement mismatch")
        print(f"staged {digest}; stock service was not restarted")
    elif args.command == "restart":
        confirm(args.confirm); before = identity(ssh)
        if before.get("dropins") not in ("", DROPIN):
            raise IdentityError("unexpected ac50.service drop-in")
        if parse_facts(ssh.fixed("restart", 15)) != {"restart_requested": "1"}:
            raise RemoteError("restart acknowledgement mismatch")
        settle(ssh, DROPIN, before["main_pid"])
        print("ac50.service restarted exactly once and settled")
    elif args.command == "restore-stock":
        confirm(args.confirm); identity(ssh, False)
        if parse_facts(ssh.fixed("restore", 15)) != {"stock_restart_requested": "1"}:
            raise RemoteError("restore acknowledgement mismatch")
        settle(ssh, "")
        print("stock ac50.service restored; volatile builds retained for inspection")
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
    sub.add_parser("build"); sub.add_parser("status")
    for name in ("deploy", "restart", "restore-stock"):
        item = sub.add_parser(name); item.add_argument("--confirm")
    test = sub.add_parser("test"); test.add_argument("--level", choices=("A", "B"), default="A")
    collect = sub.add_parser("collect"); collect.add_argument("--output")
    return result


def main(argv: Sequence[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        local = args.command in ("build", "test")
        if not local and args.config is None:
            raise HilError("this command requires --config")
        ssh = None if local else Ssh(Config.load(args.config))
        return run_command(args, ssh)
    except HilError as exc:
        print(f"error: {exc}", file=sys.stderr)
        if args.command == "restart":
            print(
                f"recovery (explicit, never automatic): rerun with restore-stock --confirm {CONFIRM}",
                file=sys.stderr,
            )
        return exc.exit_code


if __name__ == "__main__":
    raise SystemExit(main())
