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


class CycleSsh:
    def __init__(self):
        self.collect_calls = 0

    def fixed(self, name, timeout=10):
        if name != "collect":
            raise AssertionError(f"unexpected fixed operation {name}")
        self.collect_calls += 1
        runtime = "absent" if self.collect_calls == 1 else "present"
        return (
            f"hil_runtime={runtime}\nactive_sha256="
            f"{'absent' if runtime == 'absent' else 'a' * 64}\n"
            "previous_sha256=absent\n"
        ).encode()


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

    def test_native_response_schema_is_exact_and_fail_closed(self):
        valid = (
            b"v=2 native=observed context=ready action_accepted=1 "
            b"command_accepted=1 event_observed=1 event_removed=0 "
            b"paint_after_arm=1 diagnostic=event_observed\n"
        )
        self.assertEqual(hil.parse_native_response(valid)["native"], "observed")
        invalid = (
            valid.replace(b"event_observed=1", b"event_observed=2"),
            valid + b"extra=1\n",
            valid.replace(b"native=observed", b"native=audible"),
            b"\xff",
        )
        for response in invalid:
            with self.assertRaises(hil.RemoteError):
                hil.parse_native_response(response)

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

    def test_m04a_mode_selects_only_the_fixed_reviewed_artifact(self):
        build = hil.parser().parse_args(["build", "--native-m04a"])
        deploy = hil.parser().parse_args([
            "deploy", "--native-m04a", "--confirm", hil.CONFIRM,
        ])
        self.assertTrue(build.native_m04a)
        self.assertTrue(deploy.native_m04a)
        self.assertEqual(
            hil.M04A_ARTIFACT,
            hil.ROOT / "target/m04a/build/neurogenic-m04a-native-test.so",
        )
        self.assertEqual(hil.M04A_SUMS.parent, hil.M04A_ARTIFACT.parent)
        with self.assertRaises(SystemExit):
            hil.parser().parse_args(["deploy", "--hil", "--native-m04a"])

    def test_m05a_mode_selects_only_the_fixed_reviewed_grid_artifact(self):
        build = hil.parser().parse_args(["build", "--native-m05a"])
        deploy = hil.parser().parse_args([
            "deploy", "--native-m05a", "--confirm", hil.CONFIRM,
        ])
        self.assertTrue(build.native_m05a)
        self.assertTrue(deploy.native_m05a)
        self.assertEqual(
            hil.M05A_ARTIFACT,
            hil.ROOT / "target/m05a/build/neurogenic-m05a-native-grid.so",
        )
        self.assertEqual(hil.M05A_SUMS.parent, hil.M05A_ARTIFACT.parent)
        with self.assertRaises(SystemExit):
            hil.parser().parse_args(["deploy", "--native-m04a", "--native-m05a"])

    def test_native_build_is_fail_fast_and_has_all_fixture_tools(self):
        completed = mock.Mock(returncode=0)
        with mock.patch.object(hil.subprocess, "run", return_value=completed) as run:
            with mock.patch.object(
                hil, "read_reviewed_artifact", return_value=(b"native", "a" * 64)
            ):
                self.assertEqual(hil.build_native_artifact(), (b"native", "a" * 64))
        command = run.call_args.args[0]
        self.assertIn("nixpkgs#qemu", command)
        self.assertIn("nixpkgs#llvmPackages_21.llvm", command)
        shell_program = command[-1]
        self.assertTrue(shell_program.startswith("set -eu;"))
        self.assertLess(
            shell_program.index("verify-native-abi.sh"),
            shell_program.index("device/m04a/scripts/build.sh"),
        )

    def test_native_grid_build_reuses_abi_fixture_before_fixed_build(self):
        completed = mock.Mock(returncode=0)
        with mock.patch.object(hil.subprocess, "run", return_value=completed) as run:
            with mock.patch.object(
                hil, "read_reviewed_artifact", return_value=(b"grid", "b" * 64)
            ):
                self.assertEqual(hil.build_native_grid_artifact(), (b"grid", "b" * 64))
        command = run.call_args.args[0]
        self.assertIn("nixpkgs#qemu", command)
        shell_program = command[-1]
        self.assertLess(
            shell_program.index("verify-native-abi.sh"),
            shell_program.index("device/m05a/scripts/build.sh"),
        )

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
        self.assertIn("'/run/neurogenic-hil/response.pending'", hil.RESTORE)
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

    def test_level_b_failure_prints_expected_actual_and_first_transition(self):
        replies = [
            completed(b"v=1 open=0 track=1 step=1 bits=0000 flow=consume repaint=1 restore=0 pending_repaint=0\n"),
            completed(b"v=1 open=1 track=1 step=1 bits=0000 flow=consume repaint=1 restore=0 pending_repaint=1\n"),
            completed(b"v=1 open=0 track=1 step=1 bits=0000 flow=forward repaint=0 restore=0 pending_repaint=1\n"),
            completed(b"v=1 open=0 track=1 step=1 bits=0000 flow=forward repaint=0 restore=0 pending_repaint=1\n"),
            completed(b"v=1 open=0 track=1 step=1 bits=0000 flow=forward repaint=0 restore=0 pending_repaint=0\n"),
            completed(b"v=1 open=0 track=1 step=1 bits=0000 flow=forward repaint=0 restore=0 pending_repaint=0\n"),
            completed(b"v=1 open=0 track=1 step=1 bits=0000 flow=forward repaint=0 restore=0 pending_repaint=0\n"),
            completed(b"v=1 open=0 track=1 step=1 bits=0000 flow=consume repaint=1 restore=0 pending_repaint=0\n"),
        ]
        output = io.StringIO()
        with contextlib.redirect_stdout(output), self.assertRaises(hil.HilError):
            hil.run_level_b_scenario(hil.Ssh(self.config, Runner(replies)))
        text = output.getvalue()
        self.assertIn("Command: SELECT 4", text)
        self.assertIn('Expected: {"track": "4"}', text)
        self.assertIn('Actual: {"bits": "0000"', text)
        self.assertIn("First unexpected state transition: SELECT 4", text)

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


class RunCycleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        root = Path(self.temp.name)
        self.run_root = root / "runs"
        self.run_lock = root / "run.lock"
        self.args = type(
            "Args",
            (),
            {
                "level": "B",
                "scenario": True,
                "confirm": hil.CONFIRM,
            },
        )()
        self.initial = identity_output().decode()
        self.identity_facts = hil.parse_facts(identity_output())
        self.records = [
            {
                "level": "B",
                "command": "QUERY",
                "expected": {"open": "0"},
                "actual": {"open": "0"},
                "pass": True,
                "duration_ms": 1.0,
            }
        ]

    def tearDown(self):
        self.temp.cleanup()

    def patches(self, **overrides):
        def scenario(_ssh, records):
            records.extend(self.records)
            return records

        defaults = {
            "identity": mock.Mock(return_value=dict(self.identity_facts)),
            "build_hil_artifact": mock.Mock(return_value=(b"ELF", "a" * 64)),
            "deploy_artifact": mock.Mock(return_value=None),
            "activate_artifact": mock.Mock(return_value={**self.identity_facts, "main_pid": "202", "dropins": hil.DROPIN}),
            "wait_bridge": mock.Mock(return_value=None),
            "execute_level_b_scenario": mock.Mock(side_effect=scenario),
            "restore_stock": mock.Mock(return_value={**self.identity_facts, "main_pid": "303"}),
            "git_commit": mock.Mock(return_value="b" * 40),
        }
        defaults.update(overrides)
        stack = contextlib.ExitStack()
        stack.enter_context(mock.patch.object(hil, "RUN_ROOT", self.run_root))
        stack.enter_context(mock.patch.object(hil, "RUN_LOCK", self.run_lock))
        for name, replacement in defaults.items():
            stack.enter_context(mock.patch.object(hil, name, replacement))
        return stack, defaults

    def run_cycle(self, ssh=None, **overrides):
        stack, patched = self.patches(**overrides)
        output = io.StringIO()
        with stack, contextlib.redirect_stdout(output):
            result = hil.orchestrate_run(self.args, CycleSsh() if ssh is None else ssh)
        return result, output.getvalue(), patched

    def only_report(self):
        directories = list(self.run_root.iterdir())
        self.assertEqual(len(directories), 1)
        return json.loads((directories[0] / "report.json").read_text())

    def test_successful_full_run_generates_reports_and_restores(self):
        result, output, patched = self.run_cycle()
        self.assertEqual(result, 0)
        self.assertIn("Overall PASS", output)
        report = self.only_report()
        self.assertEqual(report["status"], "pass")
        self.assertEqual(report["artifact_sha256"], "a" * 64)
        self.assertEqual(report["restoration"]["status"], "pass")
        self.assertEqual(report["assertions"], {"passed": 1, "failed": 0})
        self.assertTrue((next(self.run_root.iterdir()) / "summary.txt").is_file())
        patched["restore_stock"].assert_called_once()
        self.assertFalse(self.run_lock.exists())

    def test_build_failure_does_not_restart_stock(self):
        failure = mock.Mock(side_effect=hil.HilError("build failed"))
        stack, patched = self.patches(build_hil_artifact=failure)
        with stack, contextlib.redirect_stdout(io.StringIO()), self.assertRaises(hil.RunCycleError):
            hil.orchestrate_run(self.args, CycleSsh())
        patched["restore_stock"].assert_not_called()
        self.assertEqual(self.only_report()["restoration"]["status"], "not-required")

    def test_mutating_failures_attempt_one_restoration(self):
        for function, error in (
            ("deploy_artifact", hil.RemoteError("deploy failed")),
            ("activate_artifact", hil.RemoteError("activation failed")),
            ("execute_level_b_scenario", hil.RemoteError("test timeout")),
        ):
            with self.subTest(function=function):
                with tempfile.TemporaryDirectory() as temporary:
                    self.run_root = Path(temporary) / "runs"
                    self.run_lock = Path(temporary) / "lock"
                    replacement = mock.Mock(side_effect=error)
                    stack, patched = self.patches(**{function: replacement})
                    with stack, contextlib.redirect_stdout(io.StringIO()), self.assertRaises(hil.RunCycleError):
                        hil.orchestrate_run(self.args, CycleSsh())
                    patched["restore_stock"].assert_called_once()
                    self.assertEqual(self.only_report()["restoration"]["status"], "pass")

    def test_assertion_failure_is_reported_then_restored(self):
        failed = [{**self.records[0], "pass": False, "actual": {"open": "1"}}]
        def scenario(_ssh, records):
            records.extend(failed)
            return records
        stack, patched = self.patches(
            execute_level_b_scenario=mock.Mock(side_effect=scenario)
        )
        output = io.StringIO()
        with stack, contextlib.redirect_stdout(output), self.assertRaises(hil.RunCycleError):
            hil.orchestrate_run(self.args, CycleSsh())
        report = self.only_report()
        self.assertEqual(report["assertions"], {"passed": 0, "failed": 1})
        self.assertIn("Command: QUERY", output.getvalue())
        self.assertIn("First unexpected state transition: QUERY", output.getvalue())
        patched["restore_stock"].assert_called_once()

    def test_ssh_disconnect_before_mutation_does_not_restore(self):
        stack, patched = self.patches(
            identity=mock.Mock(side_effect=hil.TransportError("SSH disconnected"))
        )
        with stack, contextlib.redirect_stdout(io.StringIO()), self.assertRaises(hil.RunCycleError):
            hil.orchestrate_run(self.args, CycleSsh())
        patched["restore_stock"].assert_not_called()

    def test_restoration_failure_is_prominent_and_preserves_original_error(self):
        stack, _patched = self.patches(
            execute_level_b_scenario=mock.Mock(side_effect=hil.RemoteError("test failed")),
            restore_stock=mock.Mock(side_effect=hil.TransportError("SSH unavailable")),
        )
        with stack, contextlib.redirect_stdout(io.StringIO()), self.assertRaisesRegex(
            hil.RunCycleError, "MAY STILL BE RUNNING THE TEST MOD"
        ) as raised:
            hil.orchestrate_run(self.args, CycleSsh())
        self.assertIn("test failed", str(raised.exception))
        report = self.only_report()
        self.assertEqual(report["restoration"]["status"], "fail")

    def test_unexpected_dropin_and_incomplete_runtime_are_refused(self):
        unexpected = {**self.identity_facts, "dropins": "/unexpected.conf"}
        stack, patched = self.patches(identity=mock.Mock(return_value=unexpected))
        with stack, contextlib.redirect_stdout(io.StringIO()), self.assertRaises(hil.RunCycleError):
            hil.orchestrate_run(self.args, CycleSsh())
        patched["build_hil_artifact"].assert_not_called()

        with tempfile.TemporaryDirectory() as temporary:
            self.run_root = Path(temporary) / "runs"
            self.run_lock = Path(temporary) / "lock"
            ssh = CycleSsh()
            ssh.collect_calls = 1
            stack, patched = self.patches()
            with stack, contextlib.redirect_stdout(io.StringIO()), self.assertRaises(hil.RunCycleError):
                hil.orchestrate_run(self.args, ssh)
            patched["build_hil_artifact"].assert_not_called()

    def test_concurrent_run_lock_is_never_destroyed(self):
        self.run_lock.mkdir(parents=True)
        (self.run_lock / "owner.json").write_text('{"pid": 7}\n')
        stack, _patched = self.patches()
        with stack, self.assertRaises(hil.RunCycleError):
            hil.orchestrate_run(self.args, CycleSsh())
        self.assertTrue(self.run_lock.is_dir())
        self.assertEqual((self.run_lock / "owner.json").read_text(), '{"pid": 7}\n')

    def test_main_returns_nonzero_for_run_failure(self):
        with mock.patch.object(hil.Config, "load", return_value=mock.Mock()), mock.patch.object(
            hil, "Ssh", return_value=mock.Mock()
        ), mock.patch.object(
            hil, "orchestrate_run", side_effect=hil.RunCycleError("failed")
        ), contextlib.redirect_stderr(io.StringIO()):
            result = hil.main([
                "--config", "fixture.json", "run", "--level", "B", "--scenario",
                "--confirm", hil.CONFIRM,
            ])
        self.assertEqual(result, hil.RunCycleError.exit_code)

    def test_level_a_run_is_host_only_and_reported(self):
        args = type(
            "Args", (), {"level": "A", "scenario": False, "confirm": None}
        )()
        with mock.patch.object(hil, "RUN_ROOT", self.run_root), mock.patch.object(
            hil, "RUN_LOCK", self.run_lock
        ), mock.patch.object(
            hil, "git_commit", return_value="b" * 40
        ), mock.patch.object(
            hil.subprocess, "run", return_value=completed()
        ), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(hil.orchestrate_run(args, None), 0)
        report = self.only_report()
        self.assertEqual(report["level"], "A")
        self.assertEqual(report["assertions"], {"passed": 1, "failed": 0})
        self.assertEqual(report["restoration"]["status"], "not-required")


class NativeExperimentTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        root = Path(self.temp.name)
        self.run_root = root / "runs"
        self.run_lock = root / "run.lock"
        self.args = type("Args", (), {
            "confirm": hil.CONFIRM,
            "confirm_native": hil.NATIVE_CONFIRM,
        })()
        self.identity_facts = hil.parse_facts(identity_output())

    def tearDown(self):
        self.temp.cleanup()

    @staticmethod
    def response(
        native, *, context="ready", action=1, accepted=0, present=0, removed=0,
        paint=0, diagnostic="not_reached",
    ):
        return (
            f"v=2 native={native} context={context} action_accepted={action} "
            f"command_accepted={accepted} event_observed={present} "
            f"event_removed={removed} paint_after_arm={paint} "
            f"diagnostic={diagnostic}\n"
        ).encode()

    @staticmethod
    def scan_response(*, attempts=1, traversed=0, tag3=0, tick=0, note=0,
                      native=0, byte_matches=0):
        return (
            f"v=1 attempts={attempts} traversed={traversed} tag3={tag3} "
            f"tick={tick} note={note} native={native} bytes={byte_matches}\n"
        ).encode()

    class NativeSsh(CycleSsh):
        def __init__(self, responses):
            super().__init__()
            self.responses = list(responses)
            self.commands = []

        def test_command(self, command):
            self.commands.append(command)
            if not self.responses:
                raise AssertionError("unexpected native command")
            result = self.responses.pop(0)
            if isinstance(result, Exception):
                raise result
            return result

    def patches(self, **overrides):
        defaults = {
            "identity": mock.Mock(return_value=dict(self.identity_facts)),
            "build_native_artifact": mock.Mock(return_value=(b"ELF", "d" * 64)),
            "deploy_artifact": mock.Mock(return_value=None),
            "activate_artifact": mock.Mock(return_value={
                **self.identity_facts, "main_pid": "202", "dropins": hil.DROPIN,
            }),
            "wait_bridge": mock.Mock(return_value=None),
            "restore_stock": mock.Mock(return_value={
                **self.identity_facts, "main_pid": "303",
            }),
            "git_commit": mock.Mock(return_value="b" * 40),
        }
        defaults.update(overrides)
        stack = contextlib.ExitStack()
        stack.enter_context(mock.patch.object(hil, "RUN_ROOT", self.run_root))
        stack.enter_context(mock.patch.object(hil, "RUN_LOCK", self.run_lock))
        stack.enter_context(mock.patch.object(hil.time, "sleep", return_value=None))
        for name, replacement in defaults.items():
            stack.enter_context(mock.patch.object(hil, name, replacement))
        return stack, defaults

    def only_report(self):
        directories = list(self.run_root.iterdir())
        self.assertEqual(len(directories), 1)
        return json.loads((directories[0] / "report.json").read_text())

    def successful_responses(self, *, skip_remove_accepted=False):
        responses = [
            self.response("idle"),
            self.response("armed", diagnostic="waiting_for_paint"),
            self.response(
                "accepted", accepted=1, paint=1, diagnostic="command_accepted"
            ),
            self.response(
                "observed", accepted=1, present=1, paint=1,
                diagnostic="event_observed",
            ),
            self.scan_response(
                attempts=1, traversed=1, tag3=1, tick=1, note=1,
                native=1, byte_matches=0,
            ),
            self.response(
                "remove_requested", accepted=1, present=1, paint=1,
                diagnostic="event_observed",
            ),
            self.response(
                "remove_accepted", accepted=1, present=1, paint=1,
                diagnostic="remove_accepted",
            ),
            self.response(
                "removed", accepted=1, present=1, removed=1, paint=1,
                diagnostic="event_removed",
            ),
            self.scan_response(
                attempts=2, traversed=0, tag3=0, tick=0, note=0,
                native=0, byte_matches=0,
            ),
        ]
        if skip_remove_accepted:
            responses[-3] = responses[-2]
        return responses

    def test_native_confirmation_is_distinct_and_required(self):
        parsed = hil.parser().parse_args([
            "native-experiment", "--confirm", hil.CONFIRM,
            "--confirm-native", hil.NATIVE_CONFIRM,
        ])
        self.assertEqual(parsed.confirm_native, hil.NATIVE_CONFIRM)
        bad = type("Args", (), {
            "confirm": hil.CONFIRM, "confirm_native": hil.CONFIRM,
        })()
        with self.assertRaises(hil.RunCycleError):
            hil.orchestrate_native_experiment(bad, mock.Mock())
        self.assertFalse(self.run_root.exists())

    def test_native_scan_response_is_strict_and_bounded(self):
        parsed = hil.parse_native_scan_response(self.scan_response(
            attempts=2, traversed=7, tag3=4, tick=2, note=1, native=1,
            byte_matches=0,
        ))
        self.assertEqual(parsed["native"], "1")
        self.assertEqual(parsed["bytes"], "0")
        with self.assertRaises(hil.RemoteError):
            hil.parse_native_scan_response(
                b"v=1 attempts=1 traversed=0 tag3=0 tick=0 note=0 native=2\n"
            )

    def test_success_adds_observes_removes_restores_and_reports(self):
        ssh = self.NativeSsh(self.successful_responses())
        stack, patched = self.patches()
        with stack, contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(hil.orchestrate_native_experiment(self.args, ssh), 0)
        self.assertEqual(ssh.commands.count("NATIVE ARM"), 1)
        self.assertEqual(ssh.commands.count("NATIVE REMOVE"), 1)
        report = self.only_report()
        self.assertEqual(report["status"], "pass")
        self.assertEqual(report["schema"], "neurogenic-native-experiment-v1")
        self.assertEqual(report["assertions"], {"passed": 6, "failed": 0})
        self.assertEqual(report["observation_scan"]["native"], "1")
        self.assertEqual(report["removal_scan"]["native"], "0")
        self.assertFalse(report["audible_playback_observed"])
        patched["restore_stock"].assert_called_once()
        self.assertFalse(self.run_lock.exists())

    def test_rapid_removal_transition_satisfies_acceptance_then_absence(self):
        ssh = self.NativeSsh(self.successful_responses(skip_remove_accepted=True))
        stack, patched = self.patches()
        with stack, contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(hil.orchestrate_native_experiment(self.args, ssh), 0)
        report = self.only_report()
        self.assertEqual(report["status"], "pass")
        self.assertEqual(report["assertions"], {"passed": 6, "failed": 0})
        self.assertEqual(ssh.commands.count("NATIVE REMOVE"), 1)
        patched["restore_stock"].assert_called_once()

    def test_skipped_removal_state_requires_sticky_absence_evidence(self):
        invalid = hil.parse_native_response(self.response(
            "removed", accepted=1, present=1, removed=0, paint=1,
            diagnostic="remove_accepted",
        ))
        self.assertFalse(hil.removal_command_accepted(invalid))

    def test_partial_arm_failure_is_terminal_and_restores(self):
        responses = [self.response("idle"), self.response("idle", action=0)]
        ssh = self.NativeSsh(responses)
        stack, patched = self.patches()
        with stack, contextlib.redirect_stdout(io.StringIO()), self.assertRaises(
            hil.RunCycleError
        ):
            hil.orchestrate_native_experiment(self.args, ssh)
        self.assertNotIn("NATIVE REMOVE", ssh.commands)
        patched["restore_stock"].assert_called_once()
        self.assertEqual(self.only_report()["status"], "fail")

    def test_removal_failure_never_retries_and_restores(self):
        responses = self.successful_responses()[:-2] + [
            self.response("failed", accepted=1, present=1),
            self.scan_response(attempts=2, traversed=4, tag3=2, tick=1, note=1),
        ]
        ssh = self.NativeSsh(responses)
        stack, patched = self.patches()
        with stack, contextlib.redirect_stdout(io.StringIO()), self.assertRaises(
            hil.RunCycleError
        ):
            hil.orchestrate_native_experiment(self.args, ssh)
        self.assertEqual(ssh.commands.count("NATIVE REMOVE"), 1)
        self.assertEqual(ssh.commands.count("NATIVE ARM"), 1)
        patched["restore_stock"].assert_called_once()

    def test_native_timeout_is_bounded_and_restores(self):
        responses = [self.response("idle"), self.response("armed")]
        responses.extend(self.response("armed") for _ in range(20))
        responses.append(self.scan_response(attempts=0))
        ssh = self.NativeSsh(responses)
        ticks = iter(range(200))
        stack, patched = self.patches()
        stack.enter_context(mock.patch.object(hil.time, "monotonic", side_effect=ticks))
        with stack, contextlib.redirect_stdout(io.StringIO()), self.assertRaises(
            hil.RunCycleError
        ):
            hil.orchestrate_native_experiment(self.args, ssh)
        self.assertEqual(ssh.commands.count("NATIVE ARM"), 1)
        self.assertNotIn("NATIVE REMOVE", ssh.commands)
        patched["restore_stock"].assert_called_once()

    def test_missing_post_arm_paint_is_reported_without_generic_timeout(self):
        responses = [
            self.response("idle"),
            self.response("armed", diagnostic="waiting_for_paint"),
            self.response("expired", diagnostic="expired_before_paint"),
            self.scan_response(attempts=0),
        ]
        ssh = self.NativeSsh(responses)
        stack, patched = self.patches()
        error = io.StringIO()
        with stack, contextlib.redirect_stdout(io.StringIO()), self.assertRaises(
            hil.RunCycleError
        ) as caught:
            hil.orchestrate_native_experiment(self.args, ssh)
        error.write(str(caught.exception))
        self.assertIn("expired_before_paint", error.getvalue())
        self.assertIn("paint_after_arm=0", error.getvalue())
        self.assertEqual(ssh.commands.count("NATIVE ARM"), 1)
        self.assertNotIn("NATIVE REMOVE", ssh.commands)
        patched["restore_stock"].assert_called_once()


class GridExperimentTests(unittest.TestCase):
    class GridSsh(CycleSsh):
        def __init__(self, responses=None):
            super().__init__()
            self.responses = list(responses or [
                b"v=2 hook=1 paint=0 ctx=0 draw=0 open=0 input=0 intent=0 ins=0 obs=0 rm=0 inv=0 state=inactive\n",
                b"v=2 hook=1 paint=12 ctx=1 draw=12 open=0 input=15 intent=5 ins=4 obs=4 rm=1 inv=0 state=ready\n",
            ])

        def test_command(self, command):
            if command != "GRID QUERY" or not self.responses:
                raise AssertionError("unexpected grid diagnostic command")
            return self.responses.pop(0)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        root = Path(self.temp.name)
        self.run_root = root / "runs"
        self.run_lock = root / "run.lock"
        self.args = type("Args", (), {
            "confirm": hil.CONFIRM,
            "confirm_grid": hil.GRID_CONFIRM,
        })()
        self.identity_facts = hil.parse_facts(identity_output())

    def tearDown(self):
        self.temp.cleanup()

    def patches(self, **overrides):
        defaults = {
            "identity": mock.Mock(return_value=dict(self.identity_facts)),
            "build_native_grid_artifact": mock.Mock(return_value=(b"ELF", "e" * 64)),
            "deploy_artifact": mock.Mock(return_value=None),
            "activate_artifact": mock.Mock(return_value={
                **self.identity_facts, "main_pid": "202", "dropins": hil.DROPIN,
            }),
            "wait_bridge": mock.Mock(return_value=None),
            "restore_stock": mock.Mock(return_value={
                **self.identity_facts, "main_pid": "303",
            }),
            "git_commit": mock.Mock(return_value="b" * 40),
        }
        defaults.update(overrides)
        stack = contextlib.ExitStack()
        stack.enter_context(mock.patch.object(hil, "RUN_ROOT", self.run_root))
        stack.enter_context(mock.patch.object(hil, "RUN_LOCK", self.run_lock))
        for name, replacement in defaults.items():
            stack.enter_context(mock.patch.object(hil, name, replacement))
        return stack, defaults

    def report(self):
        directory, = self.run_root.iterdir()
        return json.loads((directory / "report.json").read_text())

    def test_grid_response_is_strict_and_bounded(self):
        parsed = hil.parse_grid_response(self.GridSsh().responses[0])
        self.assertEqual(parsed["hook"], "1")
        self.assertEqual(parsed["state"], "inactive")
        with self.assertRaises(hil.RemoteError):
            hil.parse_grid_response(b"v=1 hook=1 state=ready\n")

    def test_success_waits_for_owner_then_restores_and_reports(self):
        ssh = self.GridSsh()
        stack, patched = self.patches()
        with stack, contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(
                hil.orchestrate_grid_experiment(self.args, ssh, lambda _prompt: "PASS"),
                0,
            )
        report = self.report()
        self.assertEqual(report["status"], "pass")
        self.assertEqual(report["owner_result"], "pass")
        self.assertTrue(report["audible_playback_observed"])
        self.assertEqual(report["diagnostics"]["after"]["rm"], "1")
        patched["restore_stock"].assert_called_once()

    def test_owner_failure_and_interruption_restore_stock(self):
        for response in ("FAIL", EOFError()):
            with self.subTest(response=response):
                with tempfile.TemporaryDirectory() as temporary:
                    self.run_root = Path(temporary) / "runs"
                    self.run_lock = Path(temporary) / "lock"
                    ssh = self.GridSsh()
                    stack, patched = self.patches()
                    def answer(_prompt):
                        if isinstance(response, Exception):
                            raise response
                        return response
                    with stack, contextlib.redirect_stdout(io.StringIO()), self.assertRaises(hil.RunCycleError):
                        hil.orchestrate_grid_experiment(self.args, ssh, answer)
                    patched["restore_stock"].assert_called_once()
                    self.assertEqual(self.report()["restoration"]["status"], "pass")

    def test_distinct_confirmation_is_required(self):
        parsed = hil.parser().parse_args([
            "grid-experiment", "--confirm", hil.CONFIRM,
            "--confirm-grid", hil.GRID_CONFIRM,
        ])
        self.assertEqual(parsed.confirm_grid, hil.GRID_CONFIRM)
        bad = type("Args", (), {
            "confirm": hil.CONFIRM, "confirm_grid": hil.CONFIRM,
        })()
        with self.assertRaises(hil.RunCycleError):
            hil.orchestrate_grid_experiment(bad, mock.Mock(), lambda _: "PASS")

    def test_owner_pass_requires_matching_bounded_grid_evidence(self):
        ssh = self.GridSsh(responses=[
            self.GridSsh().responses[0],
            b"v=2 hook=1 paint=8 ctx=1 draw=8 open=0 input=4 intent=0 ins=0 obs=0 rm=0 inv=0 state=ready\n",
        ])
        stack, patched = self.patches()
        with stack, contextlib.redirect_stdout(io.StringIO()), self.assertRaises(
            hil.RunCycleError
        ) as caught:
            hil.orchestrate_grid_experiment(self.args, ssh, lambda _prompt: "PASS")
        self.assertIn("bounded grid evidence was incomplete", str(caught.exception))
        patched["restore_stock"].assert_called_once()

    def test_restoration_failure_is_prominent_and_run_remains_failed(self):
        ssh = self.GridSsh()
        stack, _patched = self.patches(
            restore_stock=mock.Mock(side_effect=hil.RemoteError("restore timeout"))
        )
        with stack, contextlib.redirect_stdout(io.StringIO()), self.assertRaises(
            hil.RunCycleError
        ) as caught:
            hil.orchestrate_grid_experiment(self.args, ssh, lambda _prompt: "PASS")
        self.assertIn("MAY STILL BE RUNNING THE GRID MOD", str(caught.exception))
        report = self.report()
        self.assertEqual(report["status"], "fail")
        self.assertEqual(report["restoration"]["status"], "fail")


if __name__ == "__main__":
    unittest.main()
