import importlib.util
import contextlib
import io
import json
from pathlib import Path
import stat
import subprocess
import tempfile
import unittest
from unittest import mock


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

    def test_ssh_preserves_nixos_path_and_identity_uses_busybox_dd(self):
        runner = Runner([completed(identity_output())])
        with mock.patch.dict(hil.os.environ, {"PATH": "/nix/store/ssh/bin"}, clear=True):
            hil.identity(hil.Ssh(self.config, runner))
        self.assertEqual(runner.calls[0][1]["env"]["PATH"], "/nix/store/ssh/bin")
        self.assertIn('dd if="$stock" bs=4096 count=1', hil.IDENTITY)
        self.assertNotIn("head -c", hil.IDENTITY)

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
        self.assertIn(f'"$builds/{digest}.incoming"', script)
        self.assertIn(f'"$builds/{digest}.so"', script)
        self.assertIn('test "$count" -lt 3', script)
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
        self.assertEqual(hil.read_reviewed_artifact(artifact, sums), (b"reviewed", digest))
        artifact.write_bytes(b"changed")
        with self.assertRaises(hil.HilError):
            hil.read_reviewed_artifact(artifact, sums)

    def test_normal_build_and_deploy_use_the_same_m03c_artifact(self):
        self.assertEqual(hil.BUILD, hil.ROOT / "device/m03e/scripts/build-package.sh")
        self.assertEqual(
            hil.NORMAL_ARTIFACT,
            hil.ROOT / "target/m03c/build/neurogenic-m03c-ui.so",
        )
        self.assertEqual(hil.NORMAL_SUMS.parent, hil.NORMAL_ARTIFACT.parent)

    def test_only_fixed_operations_are_exposed(self):
        ssh = hil.Ssh(self.config, Runner([]))
        with self.assertRaises(hil.HilError):
            ssh.fixed("shell")
        self.assertNotIn("LD_PRELOAD", hil.IDENTITY)
        activation = hil.activation_script("a" * 64)
        self.assertEqual(activation.count("systemctl restart ac50.service"), 1)
        self.assertEqual(hil.RESTORE.count("systemctl restart ac50.service"), 1)
        self.assertNotIn("reboot", activation + hil.RESTORE)

    def test_mutations_require_exact_confirmation(self):
        for value in (None, "", "yes", hil.CONFIRM.lower()):
            with self.assertRaises(hil.HilError):
                hil.confirm(value)
        hil.confirm(hil.CONFIRM)

    def test_restart_and_restore_remote_failures_are_terminal(self):
        ssh = hil.Ssh(self.config, Runner([completed(stderr=b"systemctl failed", code=1)]))
        with self.assertRaises(hil.RemoteError):
            ssh.activate("a" * 64)
        ssh = hil.Ssh(self.config, Runner([completed(stderr=b"systemctl failed", code=1)]))
        with self.assertRaises(hil.RemoteError):
            ssh.fixed("restore")

    def test_restore_recognizes_failed_activation_dropin_by_immutable_build(self):
        self.assertIn("for library in '/run/neurogenic-hil/builds'/*.so", hil.RESTORE)
        self.assertIn("test \"$actual\" = \"$expected\" && valid=1", hil.RESTORE)
        self.assertIn("'/run/neurogenic-hil/request.pending'", hil.RESTORE)
        self.assertIn("rmdir '/run/neurogenic-hil/request.lock'", hil.RESTORE)

    def test_single_restart_and_explicit_stock_restoration_settle(self):
        digest = "a" * 64
        restarted = identity_output(main_pid="202", dropins=hil.DROPIN)
        runner = Runner([
            completed(identity_output()),
            completed(f"activated_sha256={digest}\n".encode()),
            completed(restarted),
            completed(f"active_sha256={digest}\nprevious_sha256=absent\n".encode()),
        ])
        restart_args = type(
            "Args", (), {"command": "restart", "confirm": hil.CONFIRM, "sha256": digest}
        )()
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(hil.run_command(restart_args, hil.Ssh(self.config, runner)), 0)
        self.assertEqual(len(runner.calls), 4)

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
        self.assertNotIn(" > ", hil.COLLECT)

    def test_level_b_command_is_bounded_and_parsed(self):
        response = (
            b"v=1 open=1 track=4 step=2 bits=0002 flow=consume "
            b"repaint=1 restore=0 pending_repaint=1\n"
        )
        runner = Runner([completed(response)])
        actual = hil.parse_hil_response(hil.Ssh(self.config, runner).test_command("QUERY"))
        self.assertEqual(actual["track"], "4")
        script = runner.calls[0][0][-1]
        self.assertIn('pending="$d/request.pending"', script)
        self.assertIn('lock="$d/request.lock"', script)
        self.assertIn('mkdir "$lock"', script)
        self.assertIn('> "$pending"', script)
        self.assertIn('test "$(wc -c < "$pending")" -eq 6', script)
        self.assertIn('mv "$pending" "$request"', script)
        self.assertLess(script.index('> "$pending"'), script.index('mv "$pending" "$request"'))
        self.assertNotIn('> "$request"', script)
        self.assertNotIn("systemctl", script)
        with self.assertRaises(hil.HilError):
            hil.Ssh(self.config, Runner([])).test_command("QUERY; id")
        with self.assertRaises(hil.RemoteError):
            hil.parse_hil_response(b"x" * 193)

    def test_level_b_stale_mailbox_timeout_and_repeated_commands_fail_cleanly(self):
        stale = hil.Ssh(self.config, Runner([completed(stderr=b"stale response", code=1)]))
        with self.assertRaises(hil.RemoteError):
            stale.test_command("QUERY")

        timed = hil.Ssh(self.config, Runner([subprocess.TimeoutExpired("ssh", 8)]))
        with self.assertRaises(hil.TransportError):
            timed.test_command("QUERY")

        response = (
            b"v=1 open=0 track=1 step=1 bits=0000 flow=forward "
            b"repaint=0 restore=0 pending_repaint=0\n"
        )
        runner = Runner([completed(response), completed(response)])
        ssh = hil.Ssh(self.config, runner)
        hil.parse_hil_response(ssh.test_command("QUERY"))
        hil.parse_hil_response(ssh.test_command("QUERY"))
        self.assertEqual(len(runner.calls), 2)

    def test_level_b_scenario_is_exact_and_reports_expected_and_actual(self):
        states = [
            (0, 1, 1, "0000", "consume", 1, 0),
            (1, 1, 1, "0000", "consume", 1, 0),
            (1, 4, 1, "0000", "consume", 1, 0),
            (1, 4, 4, "0000", "consume", 1, 0),
            (1, 4, 4, "0008", "consume", 1, 0),
            (1, 4, 4, "0008", "forward", 0, 0),
            (0, 4, 4, "0008", "consume", 1, 1),
            (0, 1, 1, "0000", "consume", 1, 0),
        ]
        replies = []
        for opened, track, step, bits, flow, repaint, restore in states:
            replies.append(completed(
                f"v=1 open={opened} track={track} step={step} bits={bits} "
                f"flow={flow} repaint={repaint} restore={restore} pending_repaint=1\n".encode()
            ))
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            hil.run_level_b_scenario(hil.Ssh(self.config, Runner(replies)))
        records = [json.loads(line) for line in output.getvalue().splitlines()]
        self.assertEqual(
            [record["command"] for record in records],
            ["RESET", "OPEN", "SELECT 4", "MOVE 3", "TOGGLE", "QUERY", "CLOSE", "RESET"],
        )
        self.assertTrue(all(record["pass"] for record in records))
        self.assertTrue(all("actual" in record and "expected" in record for record in records))

    def test_repeatable_activation_never_overwrites_library(self):
        digest = "b" * 64
        script = hil.activation_script(digest)
        self.assertIn(f"/builds/{digest}.so", script)
        self.assertNotIn(f"> '/run/neurogenic-hil/builds/{digest}.so'", script)
        self.assertIn("active.sha256", script)
        self.assertIn("previous.sha256", script)

    def test_rollback_uses_verified_previous_hash(self):
        previous = "c" * 64
        restarted = identity_output(main_pid="202", dropins=hil.DROPIN)
        runner = Runner([
            completed(identity_output(dropins=hil.DROPIN)),
            completed(f"active_sha256={'a' * 64}\nprevious_sha256={previous}\n".encode()),
            completed(f"activated_sha256={previous}\n".encode()),
            completed(restarted),
        ])
        args = type("Args", (), {"command": "rollback", "confirm": hil.CONFIRM})()
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(hil.run_command(args, hil.Ssh(self.config, runner)), 0)

    def test_parser_rejects_unknown_commands(self):
        with self.assertRaises(SystemExit):
            hil.parser().parse_args(["arbitrary-shell"])


if __name__ == "__main__":
    unittest.main()
