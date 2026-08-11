import math
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def reference_speed(power_watts, rider_mass_kg=82.0):
    wheel_power = power_watts * 0.97
    rolling = 0.005 * (rider_mass_kg + 10.0) * 9.80665
    aerodynamic = 0.5 * 1.225 * 0.40
    low, high = 0.0, 40.0
    for _ in range(60):
        speed = (low + high) / 2.0
        if rolling * speed + aerodynamic * speed**3 < wheel_power:
            low = speed
        else:
            high = speed
    return (low + high) / 2.0


class RoadSpeedModelTests(unittest.TestCase):
    def test_reference_model_is_monotonic_and_finite(self):
        speeds = [reference_speed(p) for p in (50, 100, 150, 153, 200, 300, 500)]
        self.assertTrue(all(math.isfinite(v) and v > 0 for v in speeds))
        self.assertEqual(speeds, sorted(speeds))

    def test_firmware_uses_required_constants_and_crank_power_directly(self):
        source = (ROOT / "src" / "road_speed_model.cpp").read_text()
        header = (ROOT / "src" / "road_speed_model.h").read_text()
        self.assertIn("crank_power_watts) * kDrivetrainEfficiency", source)
        self.assertNotIn("* 2.0", source)
        for token in ("0.97F", "1.225F", "0.40F", "0.005F", "9.80665F", "10.0F"):
            self.assertIn(token, header)

    def test_ride_distance_is_integrated_and_average_is_distance_over_time(self):
        source = (ROOT / "src" / "ride_log.cpp").read_text()
        self.assertIn("estimated_distance_meters_ +=", source)
        self.assertIn("estimated_distance_meters_ / moving_seconds_", source)
        self.assertIn("elapsed_us <= 250000", source)
        self.assertIn("sample.cadence_rpm > 0.0F", source)

    def test_legacy_last_ride_has_explicit_v1_migration(self):
        source = (ROOT / "src" / "settings_storage.cpp").read_text()
        self.assertIn("struct LastRideSummaryV1", source)
        self.assertIn("size == sizeof(LastRideSummaryV1)", source)
        self.assertIn("summary.road_model_version = 0", source)

    def test_mqtt_remains_si_native(self):
        mqtt = (ROOT / "src" / "mqtt_notifier.cpp").read_text()
        self.assertIn('"m", "distance"', mqtt)
        self.assertIn('"m/s", "speed"', mqtt)


if __name__ == "__main__":
    unittest.main()
