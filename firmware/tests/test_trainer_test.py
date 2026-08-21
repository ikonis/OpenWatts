from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
WEB = (ROOT / "src" / "web_ui.cpp").read_text(encoding="utf-8")
WIFI = (ROOT / "src" / "setup_wifi.cpp").read_text(encoding="utf-8")
MAIN = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8")


class TrainerTestContract(unittest.TestCase):
    def test_page_uses_public_ss2k_webui_interfaces(self):
        self.assertIn('href=/trainer-test', WEB)
        self.assertIn('runtimeConfigJSON', WEB)
        self.assertIn('configJSON', WEB)
        self.assertIn("/ergmode", WEB)
        self.assertIn("/targetwattsslider", WEB)

    def test_automated_control_has_safe_release(self):
        self.assertIn("ABORT TEST", WEB)
        self.assertIn("targetwattsslider','disable", WEB)
        self.assertIn("ergmode','disable", WEB)
        self.assertIn("Cadence stopped", WEB)
        self.assertIn("SmartSpin2K control was lost", WEB)

    def test_test_lease_is_volatile_and_guards_ride_logging(self):
        self.assertIn('/api/trainer-test', WIFI)
        self.assertIn('kTrainerTestLeaseUs', WIFI)
        self.assertIn('trainer_test_until_us_', WIFI)
        self.assertIn('!g_setup_wifi.trainerTestActive()', MAIN)


if __name__ == "__main__":
    unittest.main()
