import importlib.util
import tempfile
import unittest
from pathlib import Path
from unittest import mock


MODULE_PATH = Path(__file__).parents[1] / "tools" / "b1_p2_inspect.py"
SPEC = importlib.util.spec_from_file_location("b1_p2_inspect", MODULE_PATH)
assert SPEC and SPEC.loader
INSPECT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(INSPECT)


def inventory(result):
    return {(item["method"], item["path"]): item for item in result["endpoint_inventory"]}


class InspectionTests(unittest.TestCase):
    def test_headers_allowlist_excludes_identifying_and_security_fields(self):
        result = INSPECT.analyze_headers(
            "HTTP/1.1 200 OK\r\nServer: AZ0X Web Server/0.5.1+git9826367\r\n"
            "Content-Type: text/html\r\nSet-Cookie: secret\r\nX-Device-Serial: private\r\n\r\n"
        )
        self.assertEqual(result["status"], "HTTP/1.1 200 OK")
        self.assertNotIn("set-cookie", result["headers"])
        self.assertNotIn("x-device-serial", result["headers"])

    def test_html_extracts_paths_but_redacts_query_and_host(self):
        result = INSPECT.analyze_html(
            '<script src="https://device.invalid/static/main.js?serial=private"></script>'
        )
        self.assertEqual(result["script_sources"], ["/static/main.js"])

    def test_direct_and_endpoint_object_fetch_calls_are_static_application_calls(self):
        result = INSPECT.analyze_javascript(
            'const cfg={endpoints:{currentVersion:"/version.json",upload:"/software-update",'
            'latest:"https://vendor.invalid/version?serial=private"}};'
            'fetch(cfg.endpoints.currentVersion);fetch(cfg.endpoints.upload,{method:"POST"});'
            'fetch(cfg.endpoints.latest);'
        )
        items = inventory(result)
        self.assertEqual(items[("GET_OR_UNRESOLVED", "/version.json")]["classification"], "static-application-call")
        self.assertEqual(items[("POST", "/software-update")]["classification"], "static-application-call")
        self.assertEqual(
            items[("GET_OR_UNRESOLVED", "https://vendor.invalid/version")]["classification"],
            "static-application-call",
        )
        self.assertNotIn("private", str(result))

    def test_concat_template_and_fragments_are_found_conservatively(self):
        result = INSPECT.analyze_javascript(
            '"".concat(this.baseURL,"/object-properties").concat(path);'
            'const meta=`${base}/object-meta${path}`;const root="/api/v1";'
        )
        paths = {item["path"] for item in result["endpoint_inventory"]}
        self.assertIn("/object-properties", paths)
        self.assertIn("/object-meta", paths)
        self.assertIn("/api/v1", paths)

    def test_generic_client_methods_and_websocket_are_definitions_not_runtime_proof(self):
        result = INSPECT.analyze_javascript(
            'C.prototype.getObjectProperties=function(p){return get("".concat(this.baseURL,'
            '"/object-properties").concat(p))};'
            'C.prototype.setObjectProperties=function(p,v){return fetch("".concat(this.baseURL,'
            '"/object-properties").concat(p),{method:"PUT"})};'
            'C.prototype.initialize=function(){return this.openWebSocket()};'
            'C.prototype.openWebSocket=function(){this.webSocket=new WebSocket(this.wsHost)};'
        )
        items = inventory(result)
        classifications = {item["classification"] for item in items.values()}
        self.assertIn("client-library-definition", classifications)
        self.assertEqual(result["websocket_constructor_sites"], ["this.wsHost"])
        self.assertEqual(result["client_initialize_definition_count"], 1)
        self.assertEqual(result["client_initialize_invocation_reference_count"], 0)

    def test_observed_runtime_evidence_is_explicit_and_highest_priority(self):
        result = INSPECT.analyze_javascript(
            'fetch("/version.json")', observed=["GET /version.json"]
        )
        self.assertEqual(
            inventory(result)[("GET", "/version.json")]["classification"],
            "observed-active-endpoint",
        )
        with self.assertRaises(ValueError):
            INSPECT.analyze_javascript("", observed=["EXEC /bin/sh"])

    def test_comments_and_misleading_strings_do_not_become_calls(self):
        result = INSPECT.analyze_javascript(
            '// fetch("/software-update",{method:"POST"})\n'
            'const help="example fetch(/object-method)";'
        )
        items = list(inventory(result).values())
        self.assertTrue(all(item["classification"] == "literal-or-constructed-reference" for item in items))
        self.assertFalse(result["static_application_calls"])

    def test_minified_base_urls_and_constructor_are_reported_without_execution_claim(self):
        result = INSPECT.analyze_javascript(
            'new Client({baseURL:"http://".concat(location.host,"/api/v1"),'
            'wsHost:`ws://${location.host}`});'
        )
        self.assertEqual([item["key"] for item in result["base_url_declarations"]], ["baseURL", "wsHost"])
        self.assertEqual(result["client_initialize_invocation_reference_count"], 0)

    def test_missing_inputs_produce_empty_offline_report(self):
        self.assertEqual(INSPECT.analyze_javascript("")["endpoint_inventory"], [])
        self.assertEqual(INSPECT.analyze_html("")["script_sources"], [])

    def test_oversized_and_malformed_input_handling(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fixture.bin"
            path.write_bytes(b"12345")
            with mock.patch.object(INSPECT, "MAX_INPUT_BYTES", 4):
                with self.assertRaisesRegex(ValueError, "input exceeds"):
                    INSPECT.read_text(path)
            path.write_bytes(b"\xff")
            text, evidence = INSPECT.read_text(path)
            self.assertEqual(text, "\ufffd")
            self.assertEqual(evidence["utf8_replacement_characters"], 1)

    def test_usb_and_network_output_excludes_unselected_identifiers(self):
        usb = INSPECT.analyze_usb_text(
            "Vendor ID: 09e8 Product ID: 205c CDC-NCM Audio MIDI Serial: private MAC: secret"
        )
        network = INSPECT.analyze_network_text(
            "DHCP server 192.168.155.1 host 192.168.155.8/28 serial private"
        )
        self.assertEqual(usb["vendor_ids"], ["09e8"])
        self.assertEqual(usb["product_ids"], ["205c"])
        self.assertNotIn("private", str(usb))
        self.assertNotIn("secret", str(usb))
        self.assertTrue(network["mentions_dhcp"])
        self.assertNotIn("private", str(network))


if __name__ == "__main__":
    unittest.main()
