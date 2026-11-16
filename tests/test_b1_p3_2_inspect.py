import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock


MODULE_PATH = Path(__file__).parents[1] / "tools" / "b1_p3_2_inspect.py"
SPEC = importlib.util.spec_from_file_location("b1_p3_2_inspect", MODULE_PATH)
assert SPEC and SPEC.loader
INSPECT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(INSPECT)


def proc_stat(pid=4242, start=987654):
    fields = ["0"] * 20
    fields[0] = "S"
    fields[19] = str(start)
    return f"{pid} (MPC Sample) {' '.join(fields)}\n"


class InspectionTests(unittest.TestCase):
    def write(self, directory, name, content, binary=False):
        path = Path(directory) / name
        if binary:
            path.write_bytes(content)
        else:
            path.write_text(content, encoding="utf-8")
        return path

    def test_valid_evidence_fixture_is_classified_without_passing_active_gates(self):
        systemd = INSPECT.analyze_systemd(
            "systemd 252\nType=notify\nKillMode=control-group\nMainPID=4242\n"
            "ControlGroup=/system.slice/ac50.service\nLoadState=loaded\nActiveState=active\n"
            "SubState=running\nExecStart={ path=/usr/bin/MPC Sample ; }\nUser=\nGroup=\n"
            "After=basic.target\n"
        )
        process = INSPECT.analyze_process(
            "4242 1 1000 1000 /usr/bin/MPC Sample\n" + proc_stat()
        )
        network = INSPECT.analyze_network(
            "3: usb0 state UP inet 192.168.155.1/28\n"
            "192.168.155.0/28 dev usb0 scope link\n"
        )
        self.assertEqual(systemd["facts"]["Type"]["value"], "notify")
        self.assertEqual(process["facts"]["start_ticks"]["value"], 987654)
        self.assertEqual(network["facts"]["address_route_relation"]["value"], "matching")

    def test_missing_evidence_is_explicit_and_all_gates_stay_unpassed(self):
        report = INSPECT.build_report({category: [] for category in INSPECT.CATEGORIES})
        self.assertEqual(report["missing_evidence_categories"], list(INSPECT.CATEGORIES))
        self.assertTrue(
            all(gate["status"] == "unknown" for gate in report["decision_gates"].values())
        )

    def test_partial_evidence_remains_partial(self):
        with tempfile.TemporaryDirectory() as directory:
            path = self.write(directory, "systemd.txt", "systemd 252\nType=notify\n")
            inputs = {category: [] for category in INSPECT.CATEGORIES}
            inputs["systemd"] = [path]
            report = INSPECT.build_report(inputs)
            self.assertNotIn("systemd", report["missing_evidence_categories"])
            self.assertIn("process", report["missing_evidence_categories"])
            self.assertEqual(
                report["decision_gates"]["independent_systemd_unit_feasibility"]["status"],
                "unknown",
            )

    def test_contradictory_systemd_observations_are_not_confirmed(self):
        result = INSPECT.analyze_systemd("Type=notify\nType=simple\nMainPID=1\nMainPID=2\n")
        self.assertEqual(result["facts"]["Type"]["classification"], "conflicting")
        self.assertEqual(result["facts"]["MainPID"]["classification"], "conflicting")

    def test_unexpected_systemd_fields_are_named_but_values_are_redacted(self):
        result = INSPECT.analyze_systemd(
            "Type=notify\nEnvironment=TOKEN=private-secret\nDeviceSerial=unique-123\n"
        )
        encoded = json.dumps(result, sort_keys=True)
        self.assertEqual(result["unexpected_fields"], ["DeviceSerial", "Environment"])
        self.assertNotIn("private-secret", encoded)
        self.assertNotIn("unique-123", encoded)

    def test_malformed_process_data_is_reported_without_identity_claim(self):
        result = INSPECT.analyze_process("not process data\n77 (broken) S\n")
        self.assertEqual(result["malformed_lines"], 2)
        self.assertEqual(result["facts"]["pid"]["classification"], "unknown")
        self.assertEqual(result["facts"]["start_ticks"]["classification"], "unknown")

    def test_unrelated_network_interfaces_and_addresses_are_redacted(self):
        result = INSPECT.analyze_network(
            "2: wlan-secret state UP inet 10.44.55.66/24\n"
            "3: usb0 state UP inet 192.168.155.1/28\n"
            "192.168.155.0/28 dev usb0 scope link\n"
        )
        encoded = json.dumps(result, sort_keys=True)
        self.assertEqual(result["other_address_count"], 1)
        self.assertNotIn("wlan-secret", encoded)
        self.assertNotIn("10.44.55.66", encoded)

    def test_unexpected_usb_address_does_not_satisfy_mapping_gate(self):
        result = INSPECT.analyze_network(
            "3: usb0 state UP inet 192.168.155.2/28\n"
            "192.168.155.0/28 dev usb0 scope link\n"
        )
        self.assertEqual(
            result["facts"]["expected_address_interface"]["classification"], "unknown"
        )
        self.assertEqual(result["facts"]["address_route_relation"]["classification"], "unknown")

    def test_oversized_and_invalid_encoding_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = self.write(directory, "large.txt", b"12345", binary=True)
            with mock.patch.object(INSPECT, "MAX_INPUT_BYTES", 4):
                with self.assertRaisesRegex(ValueError, "input exceeds"):
                    INSPECT.read_text(path)
            path = self.write(directory, "invalid.txt", b"\xff", binary=True)
            with self.assertRaisesRegex(ValueError, "valid UTF-8"):
                INSPECT.read_text(path)

    def test_report_redacts_source_paths_credentials_and_environment_values(self):
        with tempfile.TemporaryDirectory(prefix="serial-private-") as directory:
            path = self.write(
                directory,
                "credential-secret.txt",
                "Type=notify\nEnvironment=PASSWORD=do-not-emit\n",
            )
            inputs = {category: [] for category in INSPECT.CATEGORIES}
            inputs["systemd"] = [path]
            encoded = json.dumps(INSPECT.build_report(inputs), sort_keys=True)
            self.assertNotIn("serial-private", encoded)
            self.assertNotIn("credential-secret", encoded)
            self.assertNotIn("do-not-emit", encoded)

    def test_output_is_deterministic_across_input_order(self):
        with tempfile.TemporaryDirectory() as directory:
            first = self.write(directory, "one.txt", "Type=notify\n")
            second = self.write(directory, "two.txt", "KillMode=control-group\n")
            a = {category: [] for category in INSPECT.CATEGORIES}
            b = {category: [] for category in INSPECT.CATEGORIES}
            a["systemd"] = [first, second]
            b["systemd"] = [second, first]
            left = INSPECT.build_report(a)
            right = INSPECT.build_report(b)
            # Source hashes are evidence-set members; normalize their order for comparison.
            left["systemd"]["sources"].sort(key=lambda item: item["sha256"])
            right["systemd"]["sources"].sort(key=lambda item: item["sha256"])
            self.assertEqual(left, right)

    def test_assumptions_are_never_promoted_to_confirmed(self):
        firewall = INSPECT.analyze_firewall(
            "ip saddr 192.168.155.0/28 accept\nip saddr 192.168.155.0/28 drop\n"
        )
        self.assertEqual(firewall["facts"]["usb_relevant_rules"]["classification"], "conflicting")
        with tempfile.TemporaryDirectory() as directory:
            mount = self.write(
                directory,
                "mount.txt",
                "/run tmpfs rw,nosuid,nodev\nRUNTIME_UNIT_DIR_MODE=755\n",
            )
            systemd = self.write(
                directory,
                "systemd.txt",
                "systemd 252\nType=notify\nKillMode=control-group\n",
            )
            inputs = {category: [] for category in INSPECT.CATEGORIES}
            inputs["mount"] = [mount]
            inputs["systemd"] = [systemd]
            gates = INSPECT.build_report(inputs)["decision_gates"]
            self.assertEqual(gates["independent_systemd_unit_feasibility"]["status"], "inferred")
            self.assertNotIn("passed", {gate["status"] for gate in gates.values()})


if __name__ == "__main__":
    unittest.main()
