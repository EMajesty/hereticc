import importlib.util
import contextlib
import io
import json
from pathlib import Path
import stat
import subprocess
import tempfile
import unittest


MODULE_PATH = Path(__file__).parents[1] / "tools/neurogenic-hil/hil.py"
SPEC = importlib.util.spec_from_file_location("neurogenic_hil", MODULE_PATH)
hil = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
import sys
sys.modules[SPEC.name] = hil
SPEC.loader.exec_module(hil)


def identity_output(**changes):
    facts = {
        "schema": "neurogenic-hil-device-v1",
        "architecture": "aarch64",
        "firmware_prefix_sha256": hil.FIRMWARE_SHA,
        "service_load": "loaded",
        "service_active": "active",
        "service_sub": "running",
        "main_pid": "101",
        "dropins": "",
        "main_exe": "/usr/bin/MPC Sample",
    }
    facts.update(changes)
    return "".join(f"{key}={value}\n" for key, value in facts.items()).encode()


class Runner:
    def __init__(self, replies):
        self.replies = list(replies)
        self.calls = []

    def __call__(self, arguments, **kwargs):
        self.calls.append((arguments, kwargs))
        reply = self.replies.pop(0)
        if isinstance(reply, Exception):
            raise reply
        return reply


def completed(stdout=b"", stderr=b"", code=0):
    return subprocess.CompletedProcess([], code, stdout, stderr)


class HilTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        root = Path(self.temp.name)
        self.key = root / "key"
        self.known = root / "known_hosts"
        self.key.write_text("private fixture")
        self.key.chmod(stat.S_IRUSR | stat.S_IWUSR)
        self.known.write_text("[192.168.155.1]:2222 fixture")
        self.config = hil.Config(self.key, self.known)

    def tearDown(self):
        self.temp.cleanup()

    def test_config_accepts_only_fixed_endpoint_and_private_key(self):
        path = Path(self.temp.name) / "config.json"
        data = {
            "schema": hil.SCHEMA, "host": hil.HOST, "port": hil.PORT, "user": hil.USER,
            "identity_file": str(self.key), "known_hosts_file": str(self.known),
        }
        path.write_text(json.dumps(data))
        self.assertEqual(hil.Config.load(path), self.config)
        data["host"] = "10.0.0.1"
        path.write_text(json.dumps(data))
        with self.assertRaises(hil.HilError):
            hil.Config.load(path)
        data["host"] = hil.HOST
        self.key.chmod(0o644)
        path.write_text(json.dumps(data))
        with self.assertRaises(hil.HilError):
            hil.Config.load(path)
        self.key.chmod(0o600)
        data["identity_file"] = 7
        path.write_text(json.dumps(data))
        with self.assertRaises(hil.HilError):
            hil.Config.load(path)

    def test_ssh_is_strict_and_passwordless(self):
        runner = Runner([completed(identity_output())])
        ssh = hil.Ssh(self.config, runner)
        hil.identity(ssh)
        args = runner.calls[0][0]
        joined = " ".join(args)
        self.assertIn("StrictHostKeyChecking=yes", joined)
        self.assertIn("PasswordAuthentication=no", joined)
        self.assertIn("IdentitiesOnly=yes", joined)
        self.assertIn(str(self.known), joined)
        self.assertNotIn("accept-new", joined)

    def test_absent_device_auth_and_host_key_fail_closed(self):
        for failure in (
            subprocess.TimeoutExpired("ssh", 5),
            completed(stderr=b"Permission denied (publickey)", code=255),
            completed(stderr=b"REMOTE HOST IDENTIFICATION HAS CHANGED", code=255),
        ):
            ssh = hil.Ssh(self.config, Runner([failure]))
            with self.assertRaises((hil.TransportError, hil.RemoteError)):
                hil.identity(ssh)

    def test_wrong_device_firmware_and_process_are_rejected(self):
        variants = (
            identity_output(architecture="x86_64"),
            identity_output(firmware_prefix_sha256="0" * 64),
            identity_output(main_exe="/tmp/not-stock"),
            identity_output(main_pid="0", main_exe=""),
        )
        for output in variants:
            with self.assertRaises(hil.IdentityError):
                hil.identity(hil.Ssh(self.config, Runner([completed(output)])))

    def test_malformed_partial_and_oversized_results_are_rejected(self):
        for output in (b"not-a-property\n", b"a=1\na=2\n", b"\xff", b"a=" + b"x" * 9000):
            with self.assertRaises(hil.RemoteError):
                hil.parse_facts(output)

    def test_upload_uses_fixed_runtime_paths_and_exact_payload(self):
        data = b"ELF fixture"
        digest = __import__("hashlib").sha256(data).hexdigest()
        runner = Runner([completed(f"deployed_sha256={digest}\n".encode())])
        ssh = hil.Ssh(self.config, runner)
        self.assertEqual(hil.parse_facts(ssh.upload(data, digest)), {"deployed_sha256": digest})
        args, kwargs = runner.calls[0]
        script = args[-1]
        self.assertIn("d='/run/neurogenic-hil'", script)
        self.assertIn('incoming="$d/incoming.so"', script)
        self.assertIn("/run/neurogenic-hil/current.so", script)
        self.assertIn("/run/neurogenic-hil/previous.so", script)
        self.assertEqual(kwargs["input"], data)
        self.assertNotIn("/data/", script)
        self.assertNotIn("/media/", script)

    def test_interrupted_and_existing_deployment_fail(self):
        for reply in (
            subprocess.TimeoutExpired("ssh", 15),
            completed(stderr=b"incoming already exists", code=1),
        ):
            with self.assertRaises((hil.TransportError, hil.RemoteError)):
                hil.Ssh(self.config, Runner([reply])).upload(b"x", __import__("hashlib").sha256(b"x").hexdigest())

    def test_reviewed_artifact_must_match_inventory(self):
        artifact = Path(self.temp.name) / "mod.so"
        sums = Path(self.temp.name) / "SHA256SUMS"
        artifact.write_bytes(b"reviewed")
        digest = __import__("hashlib").sha256(b"reviewed").hexdigest()
        sums.write_text(f"{digest}  mod.so\n")
        old_artifact, old_sums = hil.ARTIFACT, hil.ARTIFACT_SUMS
        try:
            hil.ARTIFACT, hil.ARTIFACT_SUMS = artifact, sums
            self.assertEqual(hil.read_reviewed_artifact(), (b"reviewed", digest))
            artifact.write_bytes(b"changed")
            with self.assertRaises(hil.HilError):
                hil.read_reviewed_artifact()
        finally:
            hil.ARTIFACT, hil.ARTIFACT_SUMS = old_artifact, old_sums

    def test_only_fixed_operations_are_exposed(self):
        ssh = hil.Ssh(self.config, Runner([]))
        with self.assertRaises(hil.HilError):
            ssh.fixed("shell")
        self.assertNotIn("LD_PRELOAD", hil.IDENTITY)
        self.assertEqual(hil.RESTART.count("systemctl restart ac50.service"), 1)
        self.assertEqual(hil.RESTORE.count("systemctl restart ac50.service"), 1)
        self.assertNotIn("reboot", hil.RESTART + hil.RESTORE)

    def test_mutations_require_exact_confirmation(self):
        for value in (None, "", "yes", hil.CONFIRM.lower()):
            with self.assertRaises(hil.HilError):
                hil.confirm(value)
        hil.confirm(hil.CONFIRM)

    def test_restart_and_restore_remote_failures_are_terminal(self):
        for name in ("restart", "restore"):
            ssh = hil.Ssh(self.config, Runner([completed(stderr=b"systemctl failed", code=1)]))
            with self.assertRaises(hil.RemoteError):
                ssh.fixed(name)

    def test_single_restart_and_explicit_stock_restoration_settle(self):
        restarted = identity_output(
            main_pid="202", dropins=hil.DROPIN,
        )
        runner = Runner([
            completed(identity_output()),
            completed(b"restart_requested=1\n"),
            completed(restarted),
        ])
        restart_args = type(
            "Args", (), {"command": "restart", "confirm": hil.CONFIRM}
        )()
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(hil.run_command(restart_args, hil.Ssh(self.config, runner)), 0)
        self.assertEqual(len(runner.calls), 3)

        restore_runner = Runner([
            completed(restarted),
            completed(b"stock_restart_requested=1\n"),
            completed(identity_output(main_pid="303")),
        ])
        restore_args = type(
            "Args", (), {"command": "restore-stock", "confirm": hil.CONFIRM}
        )()
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(
                hil.run_command(restore_args, hil.Ssh(self.config, restore_runner)), 0
            )
        self.assertEqual(len(restore_runner.calls), 3)

    def test_collect_has_no_mutating_commands(self):
        self.assertNotIn("systemctl restart", hil.COLLECT)
        self.assertNotIn(" rm ", hil.COLLECT)
        self.assertNotIn(">", hil.COLLECT)

    def test_level_b_is_explicitly_unsupported(self):
        args = type("Args", (), {"command": "test", "level": "B"})()
        with self.assertRaises(hil.Unsupported):
            hil.run_command(args, None)

    def test_parser_rejects_unknown_commands(self):
        with self.assertRaises(SystemExit):
            hil.parser().parse_args(["arbitrary-shell"])


if __name__ == "__main__":
    unittest.main()
