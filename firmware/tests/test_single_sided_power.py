from pathlib import Path
import math
import unittest


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


class SingleSidedPowerTests(unittest.TestCase):
    def test_reference_physics_and_total_estimate(self):
        # Left-crank mechanical power: torque * angular velocity.
        left_watts = 10.0 * (2.0 * math.pi * 60.0 / 60.0)
        total_watts = left_watts * 2.0
        self.assertAlmostEqual(left_watts, 62.8319, places=3)
        self.assertAlmostEqual(total_watts, 125.6637, places=3)

    def test_multiplier_is_explicit_and_applied_before_validation_and_filter(self):
        header = (SRC / "power_estimator.h").read_text(encoding="utf-8")
        implementation = (SRC / "power_estimator.cpp").read_text(encoding="utf-8")
        self.assertIn("kSingleSidedPowerMultiplier = 2.0F", header)
        self.assertIn("measured_left_watts", implementation)
        multiply_at = implementation.index("measured_left_watts * kSingleSidedPowerMultiplier")
        validate_at = implementation.index("maximum_valid_power_watts")
        filter_at = implementation.index("median_window_[median_index_]")
        self.assertLess(multiply_at, validate_at)
        self.assertLess(validate_at, filter_at)

    def test_power_sample_is_total_but_torque_is_left_measurement(self):
        header = (SRC / "power_estimator.h").read_text(encoding="utf-8")
        self.assertIn("Estimated total rider cycling power", header)
        self.assertIn("torque_nm remain left-crank measurements", header)

    def test_downstream_consumers_use_authoritative_power_sample_once(self):
        ble = (SRC / "ble_cycling_power.cpp").read_text(encoding="utf-8")
        ride = (SRC / "ride_log.cpp").read_text(encoding="utf-8")
        road = (SRC / "road_speed_model.cpp").read_text(encoding="utf-8")
        self.assertIn("sample.power_watts", ble)
        self.assertIn("sample.power_watts", ride)
        self.assertIn("crank_power_watts) * kDrivetrainEfficiency", road)
        self.assertNotIn("SingleSidedPowerMultiplier", ble)
        self.assertNotIn("SingleSidedPowerMultiplier", ride)
        self.assertNotIn("SingleSidedPowerMultiplier", road)

    def test_total_power_limit_rejects_before_filtering(self):
        implementation = (SRC / "power_estimator.cpp").read_text(encoding="utf-8")
        self.assertIn("if (estimated_total_watts > static_cast<float>(config_.maximum_valid_power_watts))", implementation)
        self.assertLess(implementation.index("estimated_total_watts >"),
                        implementation.index("median_window_[median_index_]"))

    def test_calibration_and_tare_are_not_scaled(self):
        calibration = (SRC / "calibration.cpp").read_text(encoding="utf-8")
        self.assertNotIn("SingleSidedPowerMultiplier", calibration)
        self.assertIn("counts_per_nm", calibration)
        self.assertIn("runtime_zero_offset_counts", calibration)


if __name__ == "__main__":
    unittest.main()
