import importlib.util
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock


MODULE_PATH = Path(__file__).parents[1] / "tools" / "b1_p3_2d_manifest.py"
SPEC = importlib.util.spec_from_file_location("b1_p3_2d_manifest", MODULE_PATH)
assert SPEC and SPEC.loader
MANIFEST = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MANIFEST)


class ManifestTests(unittest.TestCase):
    def test_manifest_is_deterministic_sorted_and_inert(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = root / "collector.rs"
            second = root / "README.md"
            first.write_text("library source\n", encoding="utf-8")
            second.write_text("review notes\n", encoding="utf-8")
            left = MANIFEST.build_manifest(root, [first, second])
            right = MANIFEST.build_manifest(root, [second, first])
            self.assertEqual(left, right)
            self.assertEqual(
                [entry["path"] for entry in left["inventory"]],
                ["README.md", "collector.rs"],
            )
            self.assertTrue(all(value is False for value in left["safety"].values()))
            self.assertNotIn(str(root), json.dumps(left, sort_keys=True))

    def test_rejects_executable_autorun_deployable_duplicate_and_external_files(self):
        with tempfile.TemporaryDirectory() as directory, tempfile.TemporaryDirectory() as other:
            root = Path(directory)
            source = root / "source.rs"
            source.write_text("source", encoding="utf-8")
            os.chmod(source, 0o755)
            with self.assertRaisesRegex(ValueError, "executable"):
                MANIFEST.build_manifest(root, [source])
            os.chmod(source, 0o644)
            autorun = root / "autorun.sh"
            autorun.write_text("inert", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "deployable"):
                MANIFEST.build_manifest(root, [autorun])
            binary = root / "payload.so"
            binary.write_bytes(b"not really an elf")
            with self.assertRaisesRegex(ValueError, "deployable"):
                MANIFEST.build_manifest(root, [binary])
            with self.assertRaisesRegex(ValueError, "duplicate"):
                MANIFEST.build_manifest(root, [source, source])
            external = Path(other) / "external.rs"
            external.write_text("external", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "outside"):
                MANIFEST.build_manifest(root, [external])

    def test_rejects_oversized_inventory_input(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source.rs"
            source.write_bytes(b"12345")
            with mock.patch.object(MANIFEST, "MAX_FILE_BYTES", 4):
                with self.assertRaisesRegex(ValueError, "exceeds"):
                    MANIFEST.build_manifest(root, [source])


if __name__ == "__main__":
    unittest.main()
