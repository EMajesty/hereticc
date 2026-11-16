import importlib.util
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).parents[1]
MODULE_PATH = ROOT / "tools" / "b1_p3_2d_safety.py"
SPEC = importlib.util.spec_from_file_location("b1_p3_2d_safety", MODULE_PATH)
assert SPEC and SPEC.loader
SAFETY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SAFETY)


class SafetyTests(unittest.TestCase):
    def fixture(self, directory, source, manifest=None, main=False):
        crate = Path(directory)
        (crate / "src").mkdir()
        (crate / "Cargo.toml").write_text(
            manifest or '[package]\nname="fixture"\nversion="0.1.0"\nedition="2024"\n',
            encoding="utf-8",
        )
        (crate / "src" / "lib.rs").write_text(source, encoding="utf-8")
        if main:
            (crate / "src" / "main.rs").write_text("fn main() {}\n", encoding="utf-8")
        return crate

    def test_real_collector_is_library_only_and_structurally_safe(self):
        for name in ("neurogenic-b1-collector-model", "neurogenic-b1-collector-linux"):
            report = SAFETY.inspect(ROOT / "crates" / name)
            self.assertEqual(report["classification"], "pass")
            self.assertTrue(all(report["verified"].values()))
            self.assertTrue(report["unverified"])

    def test_rejects_process_network_unsafe_traversal_and_service_operations(self):
        source = """
use std::process::Command;
use std::net::TcpListener;
unsafe fn alter() { let _ = \"systemctl restart ac50\"; }
fn broad() { let _ = std::fs::read_dir(\"/\"); }
"""
        with tempfile.TemporaryDirectory() as directory:
            report = SAFETY.inspect(self.fixture(directory, source))
            self.assertEqual(report["classification"], "reject")
            self.assertIn("unbounded or arbitrary process execution", report["violations"])
            self.assertIn("network API/listener", report["violations"])
            self.assertIn("process memory or unsafe code", report["violations"])
            self.assertIn("filesystem traversal", report["violations"])
            self.assertIn(
                "prohibited operation text: systemctl restart", report["violations"]
            )

    def test_rejects_binary_targets_and_dependencies(self):
        manifest = """
[package]
name="fixture"
version="0.1.0"
edition="2024"
[dependencies]
anything="1"
"""
        with tempfile.TemporaryDirectory() as directory:
            report = SAFETY.inspect(self.fixture(directory, "pub fn inert() {}", manifest, True))
            self.assertEqual(report["classification"], "reject")
            self.assertIn("external dependencies", report["violations"])
            self.assertIn("executable source target", report["violations"])


if __name__ == "__main__":
    unittest.main()
