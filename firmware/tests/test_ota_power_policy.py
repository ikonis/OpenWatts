from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class OtaPowerPolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = (ROOT / "src" / "setup_wifi.cpp").read_text(encoding="utf-8")
        cls.web = (ROOT / "src" / "web_ui.cpp").read_text(encoding="utf-8")

    def test_battery_ota_requires_maintenance_valid_voltage_and_cutoff(self):
        self.assertIn("kMinimumBatteryOtaVoltage = 3.75F", self.server)
        self.assertIn("OperatingPolicy::isMaintenance(*config)", self.server)
        self.assertIn("status.battery_valid", self.server)
        self.assertIn("std::isfinite(status.battery_voltage)", self.server)
        self.assertIn("status.battery_voltage >= kMinimumBatteryOtaVoltage", self.server)

    def test_usb_remains_an_unconditional_ota_power_source(self):
        self.assertIn("if (!usb_present && !safe_battery_ota)", self.server)

    def test_ota_page_matches_firmware_policy(self):
        self.assertIn("Maintenance Mode with at least 3.75 V", self.web)
        self.assertIn("s.operating_mode==='maintenance'", self.web)
        self.assertIn("Number(s.battery_voltage)>=3.75", self.web)


if __name__ == "__main__":
    unittest.main()
