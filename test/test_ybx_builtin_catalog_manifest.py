"""Native firmware catalog must match the YBX API, not require an SD plugin."""
import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src/util/YbxBuiltinCatalogManifest.h"
DEVICE = ROOT / "sd-card/plugins/ybx/device.json"

class BuiltinCatalogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        text = HEADER.read_text()
        payload = re.search(r'R"YBXJSON\((.*?)\)YBXJSON"', text, re.S)
        assert payload, "missing built-in JSON"
        cls.native = json.loads(payload.group(1))
        cls.compat = json.loads(DEVICE.read_text())

    def test_builtin_does_not_use_plugin_token_file(self):
        self.assertNotIn("file", self.native["token"])
        self.assertEqual(self.native["token"]["path"], "access_token")
        self.assertNotIn("config", self.native)
        self.assertEqual(self.native["download"]["dest_dir"], "/books/ybx")

    def test_browse_endpoints_and_fields_match_compat_source(self):
        for key in ("url", "lists_url", "items", "fields", "search", "lists_headers"):
            self.assertEqual(self.native["browse"][key], self.compat["browse"][key])

    def test_device_auth_and_epub_contract_matches(self):
        self.assertEqual(self.native["auth"], self.compat["auth"])
        for key in ("url", "require_url", "dest_dir", "filename", "sidecar"):
            self.assertEqual(self.native["download"][key], self.compat["download"][key])
        for endpoint in (
            self.native["browse"]["url"],
            self.native["browse"]["lists_url"],
            self.native["download"]["url"],
            self.native["auth"]["request"]["url"],
            self.native["auth"]["poll"]["url"],
        ):
            self.assertTrue(endpoint.startswith("https://my.ybx.app/"), endpoint)

    def test_secure_tls_configuration_supports_ybx_sha384_certificate(self):
        flags = (ROOT / "platformio.ini").read_text()
        self.assertIn("-DWOLFSSL_SHA384", flags)
        self.assertIn("-DWOLFSSL_SHA512", flags)
        self.assertNotIn("setInsecure(", (ROOT / "src/activities/ybx/YbxAccountActivity.cpp").read_text())

    def test_no_plugin_sd_manifest_required_for_native_constructor(self):
        source = (ROOT / "src/activities/plugins/PluginCatalogActivity.cpp").read_text()
        self.assertIn("raw = ybxnative::kBuiltinCatalogManifest;", source)
        self.assertIn("nativeYbx ? ybxnative::loadToken(token)", source)
        self.assertIn("nativeYbx ? !ybxnative::saveToken(value)", source)
        self.assertIn('manifestPath = "builtin://ybx";', source)

if __name__=="__main__":
    unittest.main()
