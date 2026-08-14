import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


class RideDashboardTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.main = (SRC / "main.cpp").read_text(encoding="utf-8")
        cls.ride_log = (SRC / "ride_log.cpp").read_text(encoding="utf-8")
        cls.setup_wifi = (SRC / "setup_wifi.cpp").read_text(encoding="utf-8")
        cls.web = (SRC / "web_ui.cpp").read_text(encoding="utf-8")

    def test_ride_page_is_registered_and_linked(self):
        self.assertIn('.uri = "/ride"', self.setup_wifi)
        self.assertIn("webui::kRidePage", self.setup_wifi)
        self.assertIn("href=/ride>Ride</a>", self.web)

    def test_browser_reads_only_ss2k_runtime_snapshot(self):
        self.assertIn("http://smartspin2k.local/runtimeConfigJSON", self.web)
        ride_page = self.web.split("const char kRidePage[]", 1)[1].split(
            "const char kSettingsPage[]", 1
        )[0]
        self.assertNotIn("configJSON", ride_page)
        self.assertIn("Promise.allSettled", ride_page)
        self.assertIn("Number.isFinite", ride_page)

    def test_live_ride_uses_existing_authoritative_state(self):
        for field in (
            "current_ride_moving_seconds",
            "current_ride_distance_meters",
            "current_ride_speed_mps",
        ):
            self.assertIn(f'\\"{field}\\"', self.setup_wifi)
        self.assertIn("RoadModel::speedMetersPerSecond", self.main)
        self.assertIn("currentMovingSeconds", self.ride_log)
        self.assertIn("currentDistanceMeters", self.ride_log)

    def test_normal_ride_wifi_is_bounded_and_non_retrying(self):
        self.assertIn("bool ride_wifi_latched = false", self.main)
        self.assertIn("bool ride_wifi_start_attempted = false", self.main)
        self.assertIn("if (g_ride_log.candidate() && !ride_wifi_latched)", self.main)
        self.assertIn("!g_setup_wifi.active() && !ride_wifi_start_attempted", self.main)
        self.assertIn("ride_wifi_latched = false", self.main)
        self.assertIn("ride_wifi_start_attempted = false", self.main)

    def test_usb_edges_preserve_dashboard_and_finalize_a_qualified_ride(self):
        self.assertIn("USB removed; dashboard remains available", self.main)
        self.assertIn("ride_wifi_latched = true", self.main)
        self.assertIn("usb_ride_finalize_pending = g_ride_log.active()", self.main)
        self.assertIn("finishForUsbConnection", self.main)
        self.assertIn('finish(now_us, "usb_connected")', self.ride_log)


if __name__ == "__main__":
    unittest.main()
