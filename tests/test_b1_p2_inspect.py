import importlib.util
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).parents[1] / "tools" / "b1_p2_inspect.py"
SPEC = importlib.util.spec_from_file_location("b1_p2_inspect", MODULE_PATH)
assert SPEC and SPEC.loader
INSPECT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(INSPECT)


class InspectionTests(unittest.TestCase):
    def test_headers_keep_only_bounded_non_identifying_fields(self):
        result = INSPECT.analyze_headers(
            "HTTP/1.1 200 OK\r\nServer: AZ0X Web Server/0.5.1+git9826367\r\n"
            "Content-Type: text/html\r\nSet-Cookie: secret\r\n\r\n"
        )
        self.assertEqual(result["status"], "HTTP/1.1 200 OK")
        self.assertNotIn("set-cookie", result["headers"])

    def test_html_extracts_saved_script_sources_without_execution(self):
        result = INSPECT.analyze_html('<script src="/static/js/main.d096576e.js"></script>')
        self.assertEqual(result["script_sources"], ["/static/js/main.d096576e.js"])

    def test_javascript_separates_direct_calls_from_library_literals(self):
        result = INSPECT.analyze_javascript(
            'fetch("/version.json");fetch("/software-update",{method:"POST"});'
            'const unused="/api/v1/subtree";new WebSocket(url);'
        )
        inventory = {item["path"]: item["classification"] for item in result["endpoint_inventory"]}
        self.assertEqual(inventory["/version.json"], "direct-static-call")
        self.assertEqual(inventory["/software-update"], "direct-static-call")
        self.assertEqual(inventory["/api/v1/subtree"], "literal-only")
        self.assertEqual(result["websocket_constructor_count"], 1)

    def test_usb_and_network_evidence_is_redacted_to_protocol_facts(self):
        usb = INSPECT.analyze_usb_text(
            "Vendor ID: 09e8 Product ID: 205c CDC-NCM Audio MIDI Serial: private"
        )
        network = INSPECT.analyze_network_text(
            "DHCP server 192.168.155.1 host 192.168.155.8/28"
        )
        self.assertEqual(usb["vendor_ids"], ["09e8"])
        self.assertEqual(usb["product_ids"], ["205c"])
        self.assertNotIn("private", str(usb))
        self.assertTrue(network["mentions_dhcp"])


if __name__ == "__main__":
    unittest.main()
