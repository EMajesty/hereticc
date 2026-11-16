import hashlib
import importlib.util
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).parents[1]
SPEC = importlib.util.spec_from_file_location("p32l", ROOT / "tools/b1_p3_2l_package.py")
assert SPEC and SPEC.loader
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)
COLLECTOR = ROOT / "target/b1-p3-2l/p3_2l_collector"


class PackageTests(unittest.TestCase):
    def setUp(self):
        if not COLLECTOR.is_file() or hashlib.sha256(COLLECTOR.read_bytes()).hexdigest() != PACKAGE.COLLECTOR_SHA256:
            self.skipTest("reviewed P3.2l collector unavailable")

    def generate(self, name):
        output = ROOT / "target/b1-p3-2l/tests" / name
        shutil.rmtree(output, ignore_errors=True)
        self.addCleanup(lambda: shutil.rmtree(output, ignore_errors=True))
        PACKAGE.generate(ROOT, COLLECTOR, output, "1" * 40)
        return output

    def test_generation_is_reproducible_and_strictly_inspected(self):
        first, second = self.generate("first"), self.generate("second")
        names = sorted(p.relative_to(first) for p in first.rglob("*") if p.is_file())
        self.assertEqual(names, sorted(p.relative_to(second) for p in second.rglob("*") if p.is_file()))
        for name in names:
            self.assertEqual((first / name).read_bytes(), (second / name).read_bytes())
        self.assertEqual(PACKAGE.inspect(ROOT, first)["classification"], "pass")

    def test_inspector_rejects_tampering(self):
        package = self.generate("tampered")
        (package / "autorun/autorun.sh").write_text("#!/bin/sh\nsystemctl restart ac50\n")
        self.assertEqual(PACKAGE.inspect(ROOT, package)["classification"], "reject")


class WrapperStageTests(unittest.TestCase):
    def run_wrapper(self, stage=None, status=1, report=False):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            autorun = root / "autorun"
            autorun.mkdir()
            stock = root / "MPC Sample"
            stock.write_bytes(b"f" * 4096)
            run_dir = root / "run/neurogenic-b1-p3-2l"
            run_dir.parent.mkdir()
            collector = autorun / "p3_2l_collector"
            body = ["#!/bin/sh"]
            if stage:
                body.append(f"printf '{stage}\\n' > '{run_dir}/collector-stage.txt'")
            if report:
                body.append(f"printf report > '{root}/b1-p3-2c-report.txt'")
            body.append(f"exit {status}")
            collector.write_text("\n".join(body) + "\n")
            collector.chmod(0o755)
            mounts = root / "mounts"
            mounts.write_text(f"/dev/mmcblk1p1 {root} exfat rw,noexec 0 0\n")
            source = PACKAGE.wrapper_source(ROOT).decode()
            source = source.replace("PATH=/usr/sbin:/usr/bin:/sbin:/bin", f"PATH={os.environ['PATH']}")
            source = source.replace('case "$0" in /media/*/autorun/autorun.sh)', 'case "$0" in */autorun/autorun.sh)')
            source = source.replace('case "$SD" in /media/*) ;; *) exit 1 ;; esac', 'case "$SD" in /*) ;; *) exit 1 ;; esac')
            source = source.replace('component=${SD#/media/}', 'component=${SD#/}')
            source = "\n".join(": # fixture" if line.startswith('case "$component"') else line for line in source.splitlines()) + "\n"
            source = source.replace("/proc/mounts", str(mounts))
            source = source.replace('STOCK="/usr/bin/MPC Sample"', f'STOCK="{stock}"')
            source = source.replace(PACKAGE.FIRMWARE_SHA256, hashlib.sha256(stock.read_bytes()).hexdigest())
            source = source.replace(PACKAGE.COLLECTOR_SHA256, hashlib.sha256(collector.read_bytes()).hexdigest())
            source = source.replace('STAGE_DIR="/run/neurogenic-b1-p3-2l"', f'STAGE_DIR="{run_dir}"')
            wrapper = autorun / "autorun.sh"
            wrapper.write_text(source)
            wrapper.chmod(0o755)
            result = subprocess.run(["/bin/sh", str(wrapper)], timeout=3)
            marker = (root / "b1-p3-2l-startup/marker.txt").read_text()
            return result.returncode, marker, run_dir.exists()

    def test_whitelisted_stage_and_report_are_recorded_and_cleaned(self):
        status, marker, remains = self.run_wrapper("report-publication-passed", 0, True)
        self.assertEqual(status, 0)
        self.assertIn("collector_stage=report-publication-passed\n", marker)
        self.assertIn("report_publication=present\n", marker)
        self.assertFalse(remains)

    def test_missing_and_invalid_stage_do_not_become_claims(self):
        status, marker, _ = self.run_wrapper(None, 127)
        self.assertEqual(status, 127)
        self.assertIn("collector_stage=not-observed\n", marker)
        status, marker, _ = self.run_wrapper("secret-data", 1)
        self.assertEqual(status, 1)
        self.assertIn("collector_stage=invalid\n", marker)
        self.assertNotIn("secret-data", marker)


if __name__ == "__main__":
    unittest.main()
