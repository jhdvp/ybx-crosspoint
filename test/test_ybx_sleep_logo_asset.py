"""Offline regression checks for generated native four-tone YBX sleep logo."""
import re
import unittest
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]

class SleepLogoAssetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.png = Image.open(ROOT / "src/images/YbxSleepLogo4.png").convert("L")
        data = (ROOT / "src/images/YbxSleepLogo4.h").read_text()
        assert "YBX_SLEEP_LOGO_SIZE = 192" in data
        data = data.split("static const uint8_t YbxSleepLogo4[] = {", 1)[1]
        cls.bytes = bytes(int(p, 16) for p in re.findall(r"0x([0-9a-fA-F]{2})", data))
        cls.levels = [
            (cls.bytes[i // 4] >> (6 - 2*(i%4))) & 3
            for i in range(192 * 192)
        ]

    def test_palette_and_asset_size(self):
        self.assertEqual(self.png.size, (192, 192))
        self.assertEqual(len(self.bytes), 9216)
        self.assertEqual(set(self.levels), {0, 1, 2, 3})

    def test_header_and_png_match_pixel_by_pixel(self):
        self.assertEqual([p // 85 for p in self.png.getdata()], self.levels)

    def test_companion_geometry_and_orientation(self):
        def pixel(x, y):
            return self.levels[y*192+x]
        self.assertEqual(pixel(0, 0), 0)
        self.assertEqual(pixel(96, 20), 1)  # neutral card background
        self.assertEqual(pixel(110, 67), 3) # first bar bright
        self.assertEqual(pixel(110, 100), 3) # second bar bright
        self.assertEqual(pixel(76, 132), 2) # third shorter graphite accent
        self.assertGreater(pixel(131, 132), 0) # companion circle
        self.assertEqual(pixel(150, 132), 1) # outside circle, inside dark card
        self.assertEqual(pixel(110, 148), 1) # no fourth bar / no rotation
if __name__ == '__main__': unittest.main()
