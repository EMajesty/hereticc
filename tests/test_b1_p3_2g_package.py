import hashlib
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).parents[1]
sys.path.insert(0, str(ROOT / "tools"))


def load(name):
    path = ROOT / "tools" / f"{name}.py"
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


PACKAGE = load("b1_p3_2g_package")
INSPECT = load("b1_p3_2g_inspect")
REVISION = "1" * 40


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.collector = ROOT / "target/aarch64-unknown-linux-gnu/release/p3_2f_collector"
        if not self.collector.is_file() or hashlib.sha256(self.collector.read_bytes()).hexdigest() != PACKAGE.COLLECTOR_SHA256:
            self.skipTest("reviewed AArch64 collector artifact is unavailable")

    def generate(self, directory, name="one"):
        output = ROOT / "target/b1-p3-2g/tests" / f"{name}-{Path(directory).name}"
        if output.exists():
            shutil.rmtree(output)
        self.addCleanup(lambda: shutil.rmtree(output, ignore_errors=True))
        PACKAGE.generate(ROOT, self.collector, output, REVISION)
        return output

    def test_real_package_passes_strict_inspection(self):
        with tempfile.TemporaryDirectory() as directory:
            output = self.generate(directory)
            report = INSPECT.inspect(ROOT, output)
            self.assertEqual(report["classification"], "pass", report)
            self.assertTrue(all(report["verified"].values()))

    def test_generation_is_deterministic(self):
        with tempfile.TemporaryDirectory() as directory:
            first = self.generate(directory, "a")
            second = self.generate(directory, "b")
            names = sorted(path.relative_to(first) for path in first.rglob("*") if path.is_file())
            self.assertEqual(names, sorted(path.relative_to(second) for path in second.rglob("*") if path.is_file()))
            for name in names:
                self.assertEqual((first / name).read_bytes(), (second / name).read_bytes())
                self.assertEqual((first / name).stat().st_mode & 0o777, (second / name).stat().st_mode & 0o777)

    def test_rejects_media_output_existing_output_and_wrong_binary(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, "removable"):
                PACKAGE.validate_output(ROOT, Path("/media/test/package"))
            output = self.generate(directory)
            with self.assertRaisesRegex(ValueError, "already exists"):
                PACKAGE.generate(ROOT, self.collector, output, REVISION)
            wrong = Path(directory) / "wrong"
            wrong.write_bytes(b"not collector")
            candidate = ROOT / "target/b1-p3-2g/tests/wrong-artifact"
            shutil.rmtree(candidate, ignore_errors=True)
            self.addCleanup(lambda: shutil.rmtree(candidate, ignore_errors=True))
            with self.assertRaisesRegex(ValueError, "SHA-256"):
                PACKAGE.generate(ROOT, wrong, candidate, REVISION)

    def test_inspector_rejects_tampering_extra_files_symlinks_and_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            for index, mutation in enumerate(("wrapper", "extra", "symlink", "metadata")):
                output = self.generate(directory, str(index))
                autorun = output / "autorun"
                if mutation == "wrapper":
                    (autorun / "autorun.sh").write_text("#!/bin/sh\nsystemctl restart ac50\n")
                elif mutation == "extra":
                    (autorun / "unexpected.so").write_bytes(b"x")
                elif mutation == "symlink":
                    (autorun / "link").symlink_to("autorun.sh")
                else:
                    value = json.loads((autorun / "autorun.json").read_text())
                    value["value0"]["scripts"][0]["path"] = "other.sh"
                    (autorun / "autorun.json").write_text(json.dumps(value))
                self.assertEqual(INSPECT.inspect(ROOT, output)["classification"], "reject")


class WrapperSimulationTests(unittest.TestCase):
    def run_wrapper(self, firmware, collector_body, arguments=(), interrupt=False, repeats=1, match=True):
        with tempfile.TemporaryDirectory() as directory:
            autorun = Path(directory) / "autorun"
            autorun.mkdir()
            stock = Path(directory) / "MPC Sample"
            if firmware is not None:
                stock.write_bytes(firmware)
            collector = autorun / "p3_2f_collector"
            if collector_body is not None:
                collector.write_text(collector_body)
                collector.chmod(0o755)
            source = (ROOT / "device/m03b-b1-p3-2g/autorun.sh").read_text()
            source = source.replace('/media/*/autorun)', '*/autorun)')
            source = source.replace('PATH=/usr/bin:/bin', f'PATH={os.environ["PATH"]}')
            source = source.replace('STOCK="/usr/bin/MPC Sample"', f'STOCK="{stock}"')
            if firmware is not None:
                expected = hashlib.sha256(firmware[:4096]).hexdigest() if match else "0" * 64
                source = source.replace(f'FW="{PACKAGE.FIRMWARE_SHA256}"', f'FW="{expected}"')
            wrapper = autorun / "autorun.sh"
            wrapper.write_text(source)
            wrapper.chmod(0o755)
            results = []
            for _ in range(repeats):
                process = subprocess.Popen(["/bin/sh", str(wrapper), *arguments], cwd=autorun)
                if interrupt:
                    process.terminate()
                results.append(process.wait(timeout=3))
            return results[0] if repeats == 1 else results

    def test_success_failure_missing_inputs_wrong_firmware_and_arguments(self):
        firmware = b"f" * 4096
        self.assertEqual(self.run_wrapper(firmware, "#!/bin/sh\nexit 0\n"), 0)
        self.assertEqual(self.run_wrapper(firmware, "#!/bin/sh\nexit 23\n"), 23)
        self.assertNotEqual(self.run_wrapper(None, "#!/bin/sh\nexit 0\n"), 0)
        self.assertNotEqual(self.run_wrapper(firmware, None), 0)
        self.assertNotEqual(self.run_wrapper(firmware, "#!/bin/sh\nexit 0\n", match=False), 0)
        self.assertEqual(self.run_wrapper(firmware, "#!/bin/sh\nexit 0\n", ("unexpected",)), 64)
        self.assertNotEqual(self.run_wrapper(firmware, "#!/bin/sh\nexit 0\n", interrupt=True), 0)

    def test_collector_is_invoked_once_and_repeat_is_terminal(self):
        firmware = b"f" * 4096
        body = "#!/bin/sh\n[ ! -e \"$0.used\" ] || exit 9\n: > \"$0.used\"\nexit 0\n"
        self.assertEqual(self.run_wrapper(firmware, body, repeats=2), [0, 9])

    def test_unmodified_wrapper_rejects_non_media_package_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            wrapper = Path(directory) / "autorun.sh"
            source = (ROOT / "device/m03b-b1-p3-2g/autorun.sh").read_text()
            source = source.replace('PATH=/usr/bin:/bin', f'PATH={os.environ["PATH"]}')
            wrapper.write_text(source)
            wrapper.chmod(0o755)
            result = subprocess.run(
                ["/bin/sh", str(wrapper)], check=False, capture_output=True
            )
            self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
