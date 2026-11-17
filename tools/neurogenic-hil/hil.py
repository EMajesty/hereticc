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
HIL_BUILD = ROOT / "device/hil/scripts/build.sh"
NORMAL_ARTIFACT = ROOT / "target/m03c/build/neurogenic-m03c-ui.so"
NORMAL_SUMS = ROOT / "target/m03c/build/SHA256SUMS"
HIL_ARTIFACT = ROOT / "target/hil/build/neurogenic-hil-test.so"
HIL_SUMS = ROOT / "target/hil/build/SHA256SUMS"
RUNTIME = "/run/neurogenic-hil"
BUILDS = RUNTIME + "/builds"
ACTIVE = RUNTIME + "/active.sha256"
PREVIOUS = RUNTIME + "/previous.sha256"
DROPIN = "/run/systemd/system/ac50.service.d/neurogenic-hil.conf"
LEGACY_DROPIN_TEXT = "[Service]\nEnvironment=LD_PRELOAD=/run/neurogenic-hil/current.so\n"
MAX_OUTPUT = 8192


class HilError(Exception):
    exit_code = 2


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
for item in active previous; do
 path='/run/neurogenic-hil/'"$item"'.sha256'
 if test -f "$path" && ! test -L "$path"; then printf '%s_sha256=%s\n' "$item" "$(cat "$path")"
 else printf '%s_sha256=absent\n' "$item"; fi
done
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
for path in '{RUNTIME}/bridge.ready' '{RUNTIME}/request' '{RUNTIME}/response' \
 '{ACTIVE}' '{PREVIOUS}' '{RUNTIME}/current.so' '{RUNTIME}/previous.so'; do
 if test -e "$path" || test -L "$path"; then test -f "$path" && test ! -L "$path"; rm "$path"; fi
done
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
        scripts = {"identity": IDENTITY, "collect": COLLECT, "restore": RESTORE}
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
        script = f"""set -eu
d='{RUNTIME}'; request="$d/request"; response="$d/response"
test -f "$d/bridge.ready" && test ! -L "$d/bridge.ready"
test ! -e "$request" && test ! -L "$request"
test ! -e "$response" && test ! -L "$response"
( set -C; printf '%s\n' '{command}' > "$request" )
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


def run_level_b_scenario(ssh: Ssh) -> None:
    steps = [
        ("RESET", {"open": "0", "track": "1", "step": "1", "bits": "0000"}),
        ("OPEN", {"open": "1"}),
        ("OPEN", {"open": "1", "flow": "forward"}),
        ("SELECT 4", {"track": "4"}),
        ("MOVE 3", {"step": "4"}),
        ("TOGGLE", {"bits": "0008"}),
        ("TOGGLE", {"bits": "0000"}),
        ("CLOSE", {"open": "0", "restore": "1"}),
        ("SHELL", {"error": "unknown"}),
        ("RESET", {"open": "0", "track": "1", "step": "1", "bits": "0000"}),
    ]
    failed = False
    for command, expected in steps:
        actual = parse_hil_response(ssh.test_command(command))
        passed = all(actual.get(key) == value for key, value in expected.items())
        print(json.dumps({
            "level": "B", "command": command, "expected": expected,
            "actual": actual, "pass": passed,
        }, sort_keys=True))
        failed |= not passed
    if failed:
        raise RemoteError("one or more Level B state assertions failed")


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


def run_command(args: argparse.Namespace, ssh: Ssh | None) -> int:
    if args.command == "build":
        script = HIL_BUILD if args.hil else BUILD
        code = subprocess.run([str(script)], cwd=ROOT, check=False).returncode
        if code:
            raise HilError(f"fixed {'HIL' if args.hil else 'M0.3e'} build failed with status {code}")
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
        artifact, sums = (
            (HIL_ARTIFACT, HIL_SUMS) if args.hil
            else (NORMAL_ARTIFACT, NORMAL_SUMS)
        )
        data, digest = read_reviewed_artifact(artifact, sums)
        if parse_facts(ssh.upload(data, digest)) != {"deployed_sha256": digest}:
            raise RemoteError("deployment acknowledgement mismatch")
        print(f"staged immutable build {digest}; activate with restart --sha256 {digest}")
    elif args.command == "restart":
        confirm(args.confirm); before = identity(ssh)
        if before.get("dropins") not in ("", DROPIN):
            raise IdentityError("unexpected ac50.service drop-in")
        result = parse_facts(ssh.activate(args.sha256))
        if result != {"activated_sha256": args.sha256}:
            raise RemoteError("activation acknowledgement mismatch")
        settle(ssh, DROPIN, before["main_pid"])
        state = parse_facts(ssh.fixed("collect"))
        if state.get("active_sha256") != args.sha256:
            raise RemoteError("active build state does not match the requested hash")
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
        confirm(args.confirm); identity(ssh, False)
        if parse_facts(ssh.fixed("restore", 15)) != {"stock_restart_requested": "1"}:
            raise RemoteError("restore acknowledgement mismatch")
        settle(ssh, "")
        print("stock ac50.service restored; volatile HIL files removed")
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
    build = sub.add_parser("build"); build.add_argument("--hil", action="store_true")
    sub.add_parser("status")
    deploy = sub.add_parser("deploy"); deploy.add_argument("--hil", action="store_true"); deploy.add_argument("--confirm")
    restart = sub.add_parser("restart"); restart.add_argument("--sha256", required=True); restart.add_argument("--confirm")
    rollback = sub.add_parser("rollback"); rollback.add_argument("--confirm")
    restore = sub.add_parser("restore-stock"); restore.add_argument("--confirm")
    test = sub.add_parser("test")
    test.add_argument("--level", choices=("A", "B"), default="A")
    test.add_argument("--command", dest="hil_command", default="QUERY")
    test.add_argument("--scenario", action="store_true")
    test.add_argument("--confirm")
    collect = sub.add_parser("collect"); collect.add_argument("--output")
    return result


def main(argv: Sequence[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        local = args.command == "build" or (args.command == "test" and args.level == "A")
        if not local and args.config is None:
            raise HilError("this command requires --config")
        ssh = None if local else Ssh(Config.load(args.config))
        return run_command(args, ssh)
    except HilError as exc:
        print(f"error: {exc}", file=sys.stderr)
        if args.command in ("restart", "rollback"):
            print(
                f"recovery (explicit, never automatic): rerun with restore-stock --confirm {CONFIRM}",
                file=sys.stderr,
            )
        return exc.exit_code


if __name__ == "__main__":
    raise SystemExit(main())
