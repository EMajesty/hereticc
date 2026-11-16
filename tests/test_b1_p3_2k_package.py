import hashlib
import importlib.util
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).parents[1]
MODULE_PATH = ROOT / "tools/b1_p3_2k_package.py"
SPEC = importlib.util.spec_from_file_location("b1_p3_2k_package", MODULE_PATH)
assert SPEC and SPEC.loader
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)
COLLECTOR = ROOT / "target/aarch64-unknown-linux-gnu/release/p3_2f_collector"


class PackageTests(unittest.TestCase):
    def setUp(self):
        if not COLLECTOR.is_file() or hashlib.sha256(COLLECTOR.read_bytes()).hexdigest() != PACKAGE.COLLECTOR_SHA256:
            self.skipTest("reviewed P3.2f collector is unavailable")

    def generate(self, name):
        output = ROOT / "target/b1-p3-2k/tests" / name
        shutil.rmtree(output, ignore_errors=True)
        self.addCleanup(lambda: shutil.rmtree(output, ignore_errors=True))
        PACKAGE.generate(ROOT, COLLECTOR, output, "1" * 40)
        return output

    def test_generation_is_deterministic_and_inspected(self):
        first, second = self.generate("one"), self.generate("two")
        names = sorted(path.relative_to(first) for path in first.rglob("*") if path.is_file())
        self.assertEqual(names, sorted(path.relative_to(second) for path in second.rglob("*") if path.is_file()))
        for name in names:
            self.assertEqual((first / name).read_bytes(), (second / name).read_bytes())
        self.assertEqual(PACKAGE.inspect(ROOT, first)["classification"], "pass")

    def test_generator_and_inspector_reject_wrong_inputs_and_tampering(self):
        package = self.generate("tampered")
        (package / "autorun/autorun.sh").write_text("#!/bin/sh\nsystemctl restart ac50\n")
        (package / "autorun/unexpected").write_text("x")
        self.assertEqual(PACKAGE.inspect(ROOT, package)["classification"], "reject")
        wrong = ROOT / "target/b1-p3-2k/tests/wrong"
        wrong.parent.mkdir(parents=True, exist_ok=True)
        wrong.write_bytes(b"wrong")
        self.addCleanup(lambda: wrong.unlink(missing_ok=True))
        with self.assertRaisesRegex(ValueError, "collector SHA-256"):
            PACKAGE.generate(ROOT, wrong, ROOT / "target/b1-p3-2k/tests/reject", "1" * 40)


class WrapperSimulationTests(unittest.TestCase):
    def run_wrapper(
        self,
        *,
        firmware=b"f" * 4096,
        collector_body="#!/bin/sh\nexit 0\n",
        firmware_match=True,
        existing_stage=None,
        existing_report=False,
        utility_overrides=None,
        repeats=1,
    ):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            autorun = root / "autorun"
            autorun.mkdir()
            stock = root / "MPC Sample"
            if firmware is not None:
                stock.write_bytes(firmware)
            collector = autorun / "p3_2f_collector"
            if collector_body is not None:
                collector.write_text(collector_body.replace("@SD@", str(root)))
                collector.chmod(0o755)
            stage = root / "run/neurogenic-b1-p3-2k"
            stage.parent.mkdir()
            if existing_stage == "directory":
                stage.mkdir()
            elif existing_stage == "symlink":
                stage.symlink_to(root)
            if existing_report:
                (root / "b1-p3-2c-report.txt").write_text("old")
            mounts = root / "mounts"
            mounts.write_text(f"/dev/mmcblk1p1 {root} exfat rw,noexec 0 0\n")
            utilities = root / "utilities"
            utilities.mkdir()
            if utility_overrides:
                for name, body in utility_overrides.items():
                    utility = utilities / name
                    utility.write_text(body)
                    utility.chmod(0o755)
            source = (ROOT / "device/m03b-b1-p3-2k/autorun.sh").read_text()
            source = source.replace("PATH=/usr/sbin:/usr/bin:/sbin:/bin", f"PATH={utilities}:{os.environ['PATH']}")
            source = source.replace('case "$0" in /media/*/autorun/autorun.sh)', 'case "$0" in */autorun/autorun.sh)')
            source = source.replace('case "$SD" in /media/*) ;; *) exit 1 ;; esac', 'case "$SD" in /*) ;; *) exit 1 ;; esac')
            source = source.replace('component=${SD#/media/}', 'component=${SD#/}')
            source = "\n".join(
                ": # fixture component validation" if line.startswith('case "$component"') else line
                for line in source.splitlines()
            ) + "\n"
            source = source.replace("/proc/mounts", str(mounts))
            source = source.replace('STOCK="/usr/bin/MPC Sample"', f'STOCK="{stock}"')
            if firmware is not None:
                expected = hashlib.sha256(firmware[:4096]).hexdigest() if firmware_match else "0" * 64
                source = source.replace(f'FW="{PACKAGE.FIRMWARE_SHA256}"', f'FW="{expected}"')
            if collector_body is not None:
                source = source.replace(
                    f'COLLECTOR_HASH="{PACKAGE.COLLECTOR_SHA256}"',
                    f'COLLECTOR_HASH="{hashlib.sha256(collector.read_bytes()).hexdigest()}"',
                )
            source = source.replace('STAGE_DIR="/run/neurogenic-b1-p3-2k"', f'STAGE_DIR="{stage}"')
            wrapper = autorun / "autorun.sh"
            wrapper.write_text(source)
            wrapper.chmod(0o755)
            statuses = []
            for _ in range(repeats):
                statuses.append(subprocess.run(["/bin/sh", str(wrapper)], timeout=3).returncode)
            marker = root / "b1-p3-2k-startup/marker.txt"
            partial = root / "b1-p3-2k-startup/marker.partial"
            report = root / "b1-p3-2c-report.txt"
            return {
                "status": statuses[0] if repeats == 1 else statuses,
                "marker": marker.read_text() if marker.exists() else None,
                "partial": partial.exists(),
                "report": report.read_text() if report.exists() else None,
                "stage_exists": stage.exists() or stage.is_symlink(),
            }

    def assert_marker(self, result, *lines):
        self.assertIsNotNone(result["marker"])
        for line in lines:
            self.assertIn(f"{line}\n", result["marker"])

    def test_success_stages_once_reports_and_cleans(self):
        result = self.run_wrapper(
            collector_body="#!/bin/sh\nprintf 'schema=fixture\\n' > '@SD@/b1-p3-2c-report.txt'\nexit 0\n"
        )
        self.assertEqual(result["status"], 0)
        self.assertEqual(result["report"], "schema=fixture\n")
        self.assertFalse(result["stage_exists"])
        self.assert_marker(
            result, "firmware_guard=pass", "collector_file=pass", "ram_staging=pass",
            "collector_invocation=attempted", "collector_exit=0",
            "report_publication=present", "cleanup=pass",
        )

    def test_missing_collector_firmware_mismatch_and_existing_report_fail_closed(self):
        missing = self.run_wrapper(collector_body=None)
        self.assertNotEqual(missing["status"], 0)
        self.assert_marker(missing, "collector_file=fail", "collector_invocation=not-reached")
        firmware = self.run_wrapper(firmware_match=False)
        self.assertNotEqual(firmware["status"], 0)
        self.assert_marker(firmware, "firmware_guard=fail", "ram_staging=not-reached")
        report = self.run_wrapper(existing_report=True)
        self.assertNotEqual(report["status"], 0)
        self.assertEqual(report["report"], "old")
        self.assert_marker(report, "report_publication=blocked-existing")

    def test_existing_and_symlink_stage_are_rejected(self):
        for kind in ("directory", "symlink"):
            with self.subTest(kind=kind):
                result = self.run_wrapper(existing_stage=kind)
                self.assertNotEqual(result["status"], 0)
                self.assertTrue(result["stage_exists"])
                self.assert_marker(result, "ram_staging=fail", "collector_invocation=not-reached")

    def test_copy_permission_and_cleanup_failures_are_observable(self):
        copy = self.run_wrapper(utility_overrides={"cp": "#!/bin/sh\nexit 1\n"})
        self.assertNotEqual(copy["status"], 0)
        self.assert_marker(copy, "ram_staging=fail")
        chmod = self.run_wrapper(utility_overrides={"chmod": "#!/bin/sh\nexit 1\n"})
        self.assertNotEqual(chmod["status"], 0)
        self.assert_marker(chmod, "ram_staging=fail")
        cleanup = self.run_wrapper(utility_overrides={"rm": "#!/bin/sh\nexit 1\n"})
        self.assertEqual(cleanup["status"], 0)
        self.assertTrue(cleanup["stage_exists"])
        self.assert_marker(cleanup, "cleanup=fail")

    def test_loader_failure_nonzero_and_internal_timeout_preserve_status(self):
        missing_loader = self.run_wrapper(collector_body="#!/definitely/missing\nexit 0\n")
        self.assertIn(missing_loader["status"], (126, 127))
        self.assert_marker(missing_loader, f"collector_exit={missing_loader['status']}", "cleanup=pass")
        nonzero = self.run_wrapper(collector_body="#!/bin/sh\nexit 23\n")
        self.assertEqual(nonzero["status"], 23)
        self.assert_marker(nonzero, "collector_exit=23", "report_publication=absent")
        timeout = self.run_wrapper(collector_body="#!/bin/sh\nexit 124\n")
        self.assertEqual(timeout["status"], 124)
        self.assert_marker(timeout, "collector_exit=124", "cleanup=pass")

    def test_missing_and_partial_report_and_repeat_are_terminal(self):
        missing = self.run_wrapper()
        self.assert_marker(missing, "report_publication=absent")
        partial = self.run_wrapper(
            collector_body="#!/bin/sh\nprintf partial > '@SD@/.b1-p3-2c-report.partial'\nexit 1\n"
        )
        self.assert_marker(partial, "report_publication=partial", "collector_exit=1")
        repeated = self.run_wrapper(repeats=2)
        self.assertEqual(repeated["status"][0], 0)
        self.assertNotEqual(repeated["status"][1], 0)


if __name__ == "__main__":
    unittest.main()
