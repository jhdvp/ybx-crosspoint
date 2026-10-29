"""Verify X4 Pro optional I2C profile is explicit and traceable."""
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
from unittest import TestCase
from unittest.mock import patch
from run_open_qemu import run_one

class X4ProRunnerTests(TestCase):
    def make_flash_rom(self, root, chip):
        image = root / "factory.bin"
        data = bytearray(b"\xff" * 0x10100)
        data[0] = 0xe9
        data[0x8000:0x8002] = bytes((0xaa, 0x50))
        data[0x10000] = 0xe9
        image.write_bytes(data)
        qemu = root / "qemu"
        (qemu / "build-ybx").mkdir(parents=True)
        (qemu / "build-ybx" /
            ("esp32s3_rev0_rom.bin" if chip == "s3"
             else "esp32c3-rom.bin")).write_bytes(bytes(0x60000))
        return image, qemu

    def test_explicit_x4pro_s3_profile(self):
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            image, qemu = self.make_flash_rom(root, "s3")
            with patch("run_open_qemu.subprocess.run",
                       return_value=SimpleNamespace(
                           stdout="", stderr="", returncode=124)) as proc:
                report = run_one("s3", image, qemu, root / "logs", 2,
                                 None, "test-builder", x4pro_i2c=True)
            command = proc.call_args.args[0]
            self.assertEqual(command[command.index("-machine") + 1],
                             "esp32s3,ybx-x4pro-i2c=on")
            self.assertTrue(report["virtual_x4pro_i2c_attached"])
            self.assertFalse(report["virtual_x4pro_gt911_attached"])
            self.assertFalse(report["guest_driver_messages"])

    def test_c3_rejects_x4pro_bus(self):
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            image, qemu = self.make_flash_rom(root, "c3")
            with self.assertRaisesRegex(ValueError, "ESP32-S3"):
                run_one("c3", image, qemu, root / "logs", 2,
                        None, "test-builder", x4pro_i2c=True)
