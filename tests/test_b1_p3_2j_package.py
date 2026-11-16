import hashlib
import importlib.util
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).parents[1]
MODULE_PATH = ROOT / "tools/b1_p3_2j_package.py"
SPEC = importlib.util.spec_from_file_location("b1_p3_2j_package", MODULE_PATH)
assert SPEC and SPEC.loader
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)


class PackageTests(unittest.TestCase):
    def generate(self, name):
        output = ROOT / "target/b1-p3-2j/tests" / name
        shutil.rmtree(output, ignore_errors=True)
        self.addCleanup(lambda: shutil.rmtree(output, ignore_errors=True))
        PACKAGE.generate(ROOT, output, "1" * 40)
        return output

    def test_package_is_reproducible_and_strictly_inspected(self):
        first, second = self.generate("one"), self.generate("two")
        names = sorted(p.relative_to(first) for p in first.rglob("*") if p.is_file())
        self.assertEqual(names, sorted(p.relative_to(second) for p in second.rglob("*") if p.is_file()))
        for name in names:
            self.assertEqual((first / name).read_bytes(), (second / name).read_bytes())
        self.assertEqual(PACKAGE.inspect(ROOT, first)["classification"], "pass")

    def test_inspector_rejects_tampering_and_extra_executable(self):
        package = self.generate("tampered")
        (package / "autorun/autorun.sh").write_text("#!/bin/sh\nsystemctl restart ac50\n")
        (package / "autorun/extra").write_text("unexpected")
        (package / "autorun/extra").chmod(0o755)
        self.assertEqual(PACKAGE.inspect(ROOT, package)["classification"], "reject")


class MarkerSimulationTests(unittest.TestCase):
    def run_marker(self, firmware=b"f" * 4096, *, mounts=True, existing=False,
                   hash_body=None, sync_body=None, valid_directory=True):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = root / ("autorun" if valid_directory else "wrong")
            package.mkdir()
            stock = root / "MPC Sample"
            stock.write_bytes(firmware)
            mounts_file = root / "mounts"
            mounts_file.write_text(
                f"/dev/mmcblk1p1 {root} exfat rw,noexec 0 0\n" if mounts else ""
            )
            if existing:
                (root / "b1-p3-2j-startup").mkdir()
                (root / "b1-p3-2j-startup/marker.txt").write_text("old")
            utilities = root / "utilities"
            utilities.mkdir()
            if hash_body is not None:
                path = utilities / "sha256sum"
                path.write_text(hash_body)
                path.chmod(0o755)
            if sync_body is not None:
                path = utilities / "sync"
                path.write_text(sync_body)
                path.chmod(0o755)
            source = (ROOT / "device/m03b-b1-p3-2j/autorun.sh").read_text()
            source = source.replace("PATH=/usr/sbin:/usr/bin:/sbin:/bin", f"PATH={utilities}:{os.environ['PATH']}")
            source = source.replace('case "$0" in /media/*/autorun/autorun.sh)', 'case "$0" in */autorun/autorun.sh)')
            source = source.replace('case "$SD" in /media/*) ;; *) exit 1 ;; esac', 'case "$SD" in /*) ;; *) exit 1 ;; esac')
            source = source.replace('component=${SD#/media/}', 'component=${SD#/}')
            source = "\n".join(
                ": # fixture path validation"
                if line.startswith('case "$component"')
                else line
                for line in source.splitlines()
            ) + "\n"
            source = source.replace("/proc/mounts", str(mounts_file))
            source = source.replace('STOCK="/usr/bin/MPC Sample"', f'STOCK="{stock}"')
            source = source.replace(f'FW="{PACKAGE.FW_HASH}"', f'FW="{hashlib.sha256(firmware[:4096]).hexdigest()}"')
            wrapper = package / "autorun.sh"
            wrapper.write_text(source)
            wrapper.chmod(0o755)
            result = subprocess.run(["/bin/sh", str(wrapper)], capture_output=True, timeout=3)
            final = root / "b1-p3-2j-startup/marker.txt"
            partial = root / "b1-p3-2j-startup/marker.partial"
            return result.returncode, final.read_text() if final.exists() else None, partial.exists()

    def test_success_publishes_complete_marker(self):
        status, marker, partial = self.run_marker()
        self.assertEqual(status, 0)
        self.assertFalse(partial)
        self.assertEqual(
            marker,
            "schema=neurogenic-b1-startup-v1\nautorun_entered=true\n"
            "directory_valid=true\nfirmware_guard=pass\n"
            "collector_invocation=deferred\n",
        )

    def test_wrong_directory_mount_firmware_and_hash_utility_fail_closed(self):
        self.assertIsNone(self.run_marker(valid_directory=False)[1])
        self.assertIsNone(self.run_marker(mounts=False)[1])
        status, marker, _ = self.run_marker(firmware=b"wrong")
        self.assertEqual(status, 0)
        self.assertIn("firmware_guard=fail", marker)
        status, marker, _ = self.run_marker(hash_body="#!/bin/sh\nexit 127\n")
        self.assertEqual(status, 0)
        self.assertIn("firmware_guard=fail", marker)

    def test_existing_marker_and_publication_interruption_do_not_overwrite(self):
        status, marker, _ = self.run_marker(existing=True)
        self.assertNotEqual(status, 0)
        self.assertEqual(marker, "old")
        status, marker, partial = self.run_marker(sync_body="#!/bin/sh\nexit 1\n")
        self.assertNotEqual(status, 0)
        self.assertIsNone(marker)
        self.assertTrue(partial)


if __name__ == "__main__":
    unittest.main()
