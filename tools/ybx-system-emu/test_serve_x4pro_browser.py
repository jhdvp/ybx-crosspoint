"""Host-only guardrails for the X4 Pro QEMU browser click bridge."""
import unittest
import threading
from unittest.mock import Mock

from serve_x4pro_browser import FactoryX4Pro, check_touch, guest_ready_for_touch, W, H, PAGE


class BrowserTouchTests(unittest.TestCase):
    def test_readiness_does_not_depend_on_nonwhite_pixel_count(self):
        # Frame 1 is a partial/splash frame; subsequent real screens may
        # contain very little ink, including the reported frame 9.
        self.assertFalse(guest_ready_for_touch(0))
        self.assertFalse(guest_ready_for_touch(1))
        self.assertTrue(guest_ready_for_touch(2))
        self.assertTrue(guest_ready_for_touch(9))

    def test_correct_more_and_settings_coordinates(self):
        self.assertEqual(check_touch({"x":350,"y":760}),(350,760,150))
        self.assertEqual(check_touch({"x":240,"y":453,"duration_ms":180}),
                         (240,453,180))

    def test_all_panel_corners(self):
        self.assertEqual(check_touch({"x":0,"y":0}),(0,0,150))
        self.assertEqual(check_touch({"x":W-1,"y":H-1}),(479,799,150))

    def test_out_of_bounds_rejected(self):
        for x,y in ((-1,10),(480,20),(400,-1),(400,800)):
            with self.subTest(x=x,y=y):
                with self.assertRaises(ValueError):
                    check_touch({"x":x,"y":y})

    def test_invalid_or_oversized_payload_rejected(self):
        for data in (None,{"x":True,"y":20},{"x":"1","y":0},
                     {"x":12.2,"y":3},{"x":1,"y":4,"duration_ms":1000},
                     {"x":0,"y":0,"duration_ms":0},{}):
            with self.subTest(data=data):
                with self.assertRaises(ValueError):
                    check_touch(data)

    def test_reboot_sends_real_qmp_reset_and_invalidates_cached_frame(self):
        guest = FactoryX4Pro.__new__(FactoryX4Pro)
        guest.lock = threading.RLock()
        guest.tap_lock = threading.Lock()
        guest.qmp = Mock(return_value={"return": {}})
        guest.frame = 42
        guest.reboot()
        guest.qmp.assert_called_once_with("system_reset")
        self.assertEqual(guest.frame, -1)
        self.assertFalse(guest.tap_lock.locked())

    def test_reboot_qmp_error_preserves_guest_frame(self):
        guest = FactoryX4Pro.__new__(FactoryX4Pro)
        guest.lock = threading.RLock()
        guest.tap_lock = threading.Lock()
        guest.qmp = Mock(return_value={"error": {"desc": "reset unavailable"}})
        guest.frame = 42
        with self.assertRaises(RuntimeError):
            guest.reboot()
        self.assertEqual(guest.frame, 42)
        self.assertFalse(guest.tap_lock.locked())

    def test_browser_explains_unsupported_wifi_and_offers_reboot(self):
        self.assertIn("Wi-Fi radio is not emulated", PAGE)
        self.assertIn('/api/reboot', PAGE)
        self.assertIn('Restart virtual reader', PAGE)
        self.assertIn("confirm(", PAGE)

    def test_html_refuses_fake_navigation(self):
        self.assertIn("/api/tap",PAGE)
        self.assertIn("/screen.png",PAGE)
        self.assertIn("real guest-written SSD1677",PAGE)
        self.assertNotIn("More menu opened successfully",PAGE)
