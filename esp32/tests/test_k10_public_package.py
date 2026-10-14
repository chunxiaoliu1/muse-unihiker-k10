import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('k10_package', ROOT / 'tools/muse/package_k10_public.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class PublicPackageTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.build = Path(self.temp.name)
        self.config = 'CONFIG_GADGET_SDK_RUNTIME_ONLY=y\nCONFIG_MUSE_BOARD_DFROBOT_UNIHIKER_K10=y\nCONFIG_MUSE_CARTESIA_API_KEY=""\n'
        (self.build / 'sdkconfig').write_text(self.config)
        for relative in package.FLASH_FILES.values():
            path = self.build / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'public firmware fixture')
        (self.build / 'flasher_args.json').write_text(json.dumps({'flash_files': package.FLASH_FILES}))

    def test_archive_only_contains_allowlisted_public_files(self):
        (self.build / 'nvs.bin').write_bytes(b'private NVS not to be collected')
        (self.build / 'private.log').write_text('not to be collected')
        (self.build / 'release').mkdir()
        for name in ('flash.ps1', 'manifest.json'):
            (self.build / 'release' / name).write_text('obsolete package artifact')
        package.package(self.build)
        for name in ('flash.ps1', 'manifest.json'):
            self.assertFalse((self.build / 'release' / name).exists())
        with zipfile.ZipFile(self.build / 'release/unihiker-k10-muse-public.zip') as archive:
            names = archive.namelist()
            self.assertIn('usb-config/k10-usb-config.html', names)
            self.assertEqual([name for name in names if name.startswith('usb-config/')],
                             ['usb-config/k10-usb-config.html'])
            self.assertNotIn('flash.ps1', names)
            self.assertNotIn('manifest.json', names)
            self.assertIn('licenses/README.md', names)
            self.assertIn('SHA256SUMS.txt', names)
            self.assertNotIn('sdkconfig', names)
            self.assertNotIn('nvs.bin', names)
            self.assertNotIn('private.log', names)

    def test_private_config_is_rejected(self):
        (self.build / 'sdkconfig').write_text('CONFIG_GADGET_SDK_TOKEN="private"\n')
        with self.assertRaises(ValueError): package.package(self.build)

    def test_nonempty_personal_tts_key_is_rejected(self):
        (self.build / 'sdkconfig').write_text(self.config.replace('CONFIG_MUSE_CARTESIA_API_KEY=""', 'CONFIG_MUSE_CARTESIA_API_KEY="private_tts_key"'))
        with self.assertRaises(ValueError): package.package(self.build)

    def test_embedded_sdk_token_is_rejected_without_echoing_it(self):
        fixture = b'mgst_' + b'A' * 43
        (self.build / 'muse-gadget.bin').write_bytes(fixture)
        with self.assertRaises(ValueError) as error: package.package(self.build)
        self.assertNotIn(fixture.decode(), str(error.exception))

    def test_wrong_board_is_rejected(self):
        (self.build / 'sdkconfig').write_text('CONFIG_GADGET_SDK_RUNTIME_ONLY=y\nCONFIG_MUSE_BOARD_AIPI=y\n')
        with self.assertRaises(ValueError): package.package(self.build)

    def test_embedded_cartesia_key_is_rejected_without_echoing_it(self):
        fixture = b'sk_car_' + b'A' * 32
        (self.build / 'muse-gadget.bin').write_bytes(fixture)
        with self.assertRaises(ValueError) as error: package.package(self.build)
        self.assertNotIn(fixture.decode(), str(error.exception))

    def test_unexpected_flash_layout_is_rejected(self):
        layout = dict(package.FLASH_FILES)
        layout['0x11000'] = layout.pop('0x20000')
        (self.build / 'flasher_args.json').write_text(json.dumps({'flash_files': layout}))
        with self.assertRaises(ValueError): package.package(self.build)


if __name__ == '__main__': unittest.main()
