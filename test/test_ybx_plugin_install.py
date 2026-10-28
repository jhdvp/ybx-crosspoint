import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('install_ybx', ROOT / 'scripts/install_ybx_plugin.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class YbxPluginInstall(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def test_fresh_install_has_the_matching_connected_plugin(self):
        result = installer.install(self.root)
        target = self.root / 'plugins/ybx'
        manifest = json.loads((target / 'manifest.json').read_text())
        device = json.loads((target / 'device.json').read_text())
        self.assertEqual(manifest['version'], '1.7.0')
        self.assertIn('catalog.lists.auth', manifest['compatibility']['requires'])
        self.assertIn('/catalog/lists', device['browse']['lists_url'])
        self.assertFalse((target / 'token.json').exists())
        self.assertIn('config.json', result['installed'])

    def test_existing_credentials_and_custom_config_are_preserved(self):
        target = self.root / 'plugins/ybx'
        target.mkdir(parents=True)
        token = b'{"access_token":"local-test-value"}'
        config = b'{"sleep_width":"528","sleep_height":"792","custom":"retain"}'
        (target / 'token.json').write_bytes(token)
        (target / 'config.json').write_bytes(config)
        (target / 'device.json').write_text('{"old":true}')
        result = installer.install(self.root)
        self.assertEqual((target / 'token.json').read_bytes(), token)
        self.assertEqual((target / 'config.json').read_bytes(), config)
        self.assertCountEqual(result['preserved'], ['token.json', 'config.json'])
        self.assertIn('lists_url', json.loads((target / 'device.json').read_text())['browse'])
        self.assertFalse(list(target.glob('.ybx-install-*')))

    def test_legacy_location_is_updated_without_creating_a_duplicate(self):
        legacy = self.root / '.crosspoint/plugins/ybx'
        legacy.mkdir(parents=True)
        installer.install(self.root)
        self.assertTrue((legacy / 'manifest.json').is_file())
        self.assertFalse((self.root / 'plugins/ybx').exists())

    def test_two_installations_stop_before_writing(self):
        first = self.root / 'plugins/ybx'
        second = self.root / '.crosspoint/plugins/ybx'
        first.mkdir(parents=True)
        second.mkdir(parents=True)
        with self.assertRaises(ValueError):
            installer.install(self.root)
        self.assertEqual(list(first.iterdir()), [])
        self.assertEqual(list(second.iterdir()), [])

    def test_package_cannot_replace_the_reader_token(self):
        import shutil
        source = self.root / 'source'
        shutil.copytree(installer.SOURCE, source)
        manifest = json.loads((source / 'manifest.json').read_text())
        manifest['files'].append('token.json')
        (source / 'manifest.json').write_text(json.dumps(manifest))
        (source / 'token.json').write_text('packaged-test-value')
        sd = self.root / 'card'
        sd.mkdir()
        with self.assertRaises(ValueError):
            installer.install(sd, source=source)
        self.assertFalse((sd / 'plugins').exists())

    def test_symlink_cannot_redirect_installation_outside_card(self):
        with tempfile.TemporaryDirectory() as outside:
            (self.root / 'plugins').symlink_to(outside, target_is_directory=True)
            with self.assertRaises(ValueError):
                installer.install(self.root)
            self.assertEqual(list(Path(outside).iterdir()), [])


if __name__ == '__main__':
    unittest.main()
