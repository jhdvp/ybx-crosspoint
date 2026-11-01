"""Host checks for scripts/package_release.py.

Two things these tests exist to guarantee:

1. ``package()`` runs end-to-end on a fabricated build directory. The validated
   packaging path used to crash before writing any asset (``Board`` has no
   ``parent``), and only a real call to ``package()`` proves the app/factory
   naming, offset derivation, checksum and note files all line up. esptool is
   stubbed so no external binary is needed.

2. A merged factory blob is refused as an *application* image for the structural
   reason (its leading image declares less than the file holds), and never merely
   because of an incidental chip-id mismatch. That mismatch is what a wrong chip
   argument used to hide, silently turning the application/factory swap guard
   into a no-op on every ESP32-S3 board.
"""
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import package_release  # noqa: E402
from package_release import (  # noqa: E402
    PackagingError,
    classify_application_image,
    classify_factory_image,
    esp_image_info,
    release_notes_text,
)

BOARD_MAGIC = b"CROSSPOINT-BOARD-V1:"


def _tag_blob(*tags):
    return b"".join(BOARD_MAGIC + tag.encode("ascii") + b";" for tag in tags)


def _esp_image(chip_id, payload, *, segments=1):
    """Build a byte string that ``esp_image_info`` reads back as a complete image
    whose declared length equals the file length: a 24-byte header, `segments`
    8-byte segment headers (all bytes land in the last segment), then the 16-byte
    aligned padding the real format reserves for the checksum."""
    header = bytearray(24)
    header[0] = 0xE9
    header[1] = segments
    header[12:14] = chip_id.to_bytes(2, "little")
    header[23] = 0  # no appended SHA-256 trailer
    body = bytearray(header)
    position = 24
    for index in range(segments):
        seg_len = len(payload) if index == segments - 1 else 0
        segment_header = bytearray(8)
        segment_header[4:8] = seg_len.to_bytes(4, "little")
        body += segment_header
        position += 8
        if index == segments - 1:
            body += payload
            position += seg_len
    body += b"\xFF" * (((position + 16) & ~15) - position)
    return bytes(body)


def app_image(chip, board_tag):
    """A complete application image for `chip` carrying exactly one `board_tag`."""
    blob = _tag_blob(board_tag)
    payload = blob + b"\xAA" * (65536 - len(blob))
    return _esp_image(package_release.CHIP_IDS[chip], payload)


def boot_image(chip):
    """A valid small image for `chip`: stands in for the merged blob's bootloader."""
    return _esp_image(package_release.CHIP_IDS[chip], b"\x00" * 256)


def merged_factory(app_bytes, boot_bytes, parts_bytes, app_offset):
    """What esptool merge_bin produces: bootloader at 0x0, the partition table at
    0x8000, the application at its offset, and 0xFF filler across the gaps."""
    blob = bytearray(b"\xFF" * (app_offset + len(app_bytes)))
    blob[0:len(boot_bytes)] = boot_bytes
    blob[0x8000:0x8000 + len(parts_bytes)] = parts_bytes
    blob[app_offset:app_offset + len(app_bytes)] = app_bytes
    return bytes(blob)


def fake_merge(esptool, board, boot, parts, app, app_offset, destination):
    Path(destination).write_bytes(
        merged_factory(Path(app).read_bytes(), Path(boot).read_bytes(), Path(parts).read_bytes(), app_offset))


def board_by_suffix(suffix):
    return next(board for board in package_release.BOARDS if board.asset_suffix == suffix)


class PackageReleaseTest(unittest.TestCase):
    def test_factory_blob_is_refused_as_application_by_length(self):
        # Pass the board's real chip on purpose: the ONLY reason a factory blob can
        # fail the application check is then the declared-length mismatch, i.e. the
        # structural reason the swap guard is supposed to rely on -- not a chip or
        # tag accident.
        for chip, tag in (("esp32c3", "x4"), ("esp32s3", "x4pro")):
            with self.subTest(chip=chip):
                app = app_image(chip, tag)
                blob = merged_factory(app, boot_image(chip), b"\x00" * 64, 0x10000)
                self.assertEqual(esp_image_info(blob)["chip_id"], package_release.CHIP_IDS[chip])
                with self.assertRaises(PackagingError) as caught:
                    classify_application_image(blob, tag, chip)
                self.assertIn("length mismatch", str(caught.exception))

    def test_wrong_chip_would_have_been_the_old_false_rejection(self):
        # The regression the fix removed: an S3 factory blob, judged against the
        # hardcoded 'esp32c3', was rejected on the chip id alone, so the swap guard
        # could never notice a genuine C3-style name swap.
        app = app_image("esp32s3", "x4pro")
        blob = merged_factory(app, boot_image("esp32s3"), b"\x00" * 64, 0x10000)
        with self.assertRaises(PackagingError) as caught:
            classify_application_image(blob, "x4pro", "esp32c3")
        self.assertIn("chip id", str(caught.exception))

    def test_factory_blob_passes_swap_guard_with_real_chip(self):
        app = app_image("esp32c3", "x4")
        blob = merged_factory(app, boot_image("esp32c3"), b"\x00" * 64, 0x10000)
        # A correct merged blob is accepted, which proves the guard runs and rejects
        # the blob-as-application for the structural reason rather than raising here.
        classify_factory_image(blob, app, "esp32c3", 0x10000)

    def test_release_notes_use_per_board_offsets(self):
        build_state = {
            "commit": "0" * 40, "short_commit": "0000000", "branch": "test", "tags": "(none)",
            "sdk_pin": "1" * 40, "platform": "espressif32@6.0.0", "core": "PlatformIO Core 6.0.0",
        }
        app = ("ybx-2.0.2-x3-x4.bin", 1000)
        factory = ("ybx-2.0.2-x3-x4-factory.bin", 2000)

        default_notes = release_notes_text("2.0.2", "stable", board_by_suffix("x3-x4"), app, factory, "", build_state)
        self.assertIn("write_flash 0x10000", default_notes)
        self.assertIn("erase_region 0xE000 0x2000", default_notes)

        picco_notes = release_notes_text("2.0.2", "stable", board_by_suffix("picco"), app, factory, "", build_state)
        self.assertIn("write_flash 0xE0000", picco_notes)
        self.assertIn("erase_region 0x15000 0x2000", picco_notes)
        self.assertNotIn("write_flash 0x10000", picco_notes)

    def test_package_end_to_end_on_a_fake_build_dir(self):
        board = board_by_suffix("x3-x4")
        original = package_release.merge_factory
        package_release.merge_factory = fake_merge
        try:
            with tempfile.TemporaryDirectory(prefix="ybx-package-") as directory:
                root = Path(directory)
                build_root = root / ".pio" / "build"
                artefacts = build_root / board.env
                artefacts.mkdir(parents=True)
                (artefacts / "firmware.bin").write_bytes(app_image("esp32c3", "x4"))
                (artefacts / "bootloader.bin").write_bytes(boot_image("esp32c3"))
                (artefacts / "partitions.bin").write_bytes(b"\xAA" * 128)

                build_state = {
                    "commit": "0" * 40, "short_commit": "0000000", "branch": "test", "tags": "(none)",
                    "sdk_pin": "1" * 40, "platform": "espressif32@6.0.0", "core": "PlatformIO Core 6.0.0",
                }
                output = root / "dist"
                produced = package_release.package(board, "2.0.2", "stable", build_root, output, ["esptool"],
                                                  build_state, "2026-01-01 00:00:00")
                names = {path.name for path in output.iterdir()}
                self.assertEqual({p.name for p in produced},
                                 {"ybx-2.0.2-x3-x4.bin", "ybx-2.0.2-x3-x4-factory.bin"})
                self.assertTrue(all(path.is_file() and path.stat().st_size for path in produced))
                self.assertEqual(names, {
                    "ybx-2.0.2-x3-x4.bin", "ybx-2.0.2-x3-x4-factory.bin",
                    "SHA256SUMS-x3-x4.txt", "release-info-x3-x4.txt", "RELEASE_NOTES-x3-x4.md",
                })

                sums = (output / "SHA256SUMS-x3-x4.txt").read_text()
                # sha256sum format: "<64 hex>  <name>", ordered by name.
                for line in sums.splitlines():
                    digest, _, name = line.partition("  ")
                    self.assertEqual(len(digest), 64)
                    self.assertTrue(name.startswith("ybx-2.0.2-"))
                self.assertIn("application", (output / "release-info-x3-x4.txt").read_text())
        finally:
            package_release.merge_factory = original


if __name__ == "__main__":
    unittest.main()
