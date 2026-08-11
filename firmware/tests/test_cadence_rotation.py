from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class CadenceRotationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.code = (ROOT / "src" / "cadence_estimator.cpp").read_text()

    def test_installed_forward_direction_uses_negative_gyro_z(self):
        self.assertIn("forward_velocity_dps = -corrected_z", self.code)

    def test_counts_integrated_full_rotations_not_threshold_crossings(self):
        self.assertIn("forward_delta_degrees = forward_velocity_dps * dt", self.code)
        self.assertIn("forward_angle_degrees_ += forward_delta_degrees", self.code)
        self.assertIn("while (forward_angle_degrees_ >= 360.0F)", self.code)
        self.assertNotIn("armed_ && gyro_z", self.code)

    def test_reverse_rotation_is_rejected(self):
        self.assertIn("forward_velocity_dps <= -kMotionThresholdDps", self.code)
        self.assertIn("reverse_angle_degrees_ >= 90.0F", self.code)
        self.assertIn("brief", self.code)
        self.assertIn("forward_cadence_recent", self.code)

    def test_ride_zero_lifecycle_handles_real_connection_order(self):
        main = (ROOT / "src" / "main.cpp").read_text()
        zero = (ROOT / "src" / "ride_zero.cpp").read_text()
        self.assertIn("RideZeroTrigger::UsbInserted", main)
        self.assertIn("if (!ble_connected) g_ride_zero.resetLifecycle()", main)
        self.assertIn("trigger == RideZeroTrigger::BleConnected", zero)
        self.assertIn("RideZeroResult::LoadedReference", zero)
        self.assertIn("trigger != openwatts::RideZeroTrigger::Stationary", main)
        self.assertIn("trigger != openwatts::RideZeroTrigger::BleConnected", main)
        self.assertIn("persist ? g_settings.save(candidate) : ESP_OK", main)
        self.assertIn("continuous_zero_allowed = current_usb_present", main)
        self.assertIn("OperatingPolicy::isMaintenance(g_config)", main)

    def test_stationary_bias_and_arbitrary_start_are_supported(self):
        self.assertIn("gyro_z_bias_dps_ +=", self.code)
        self.assertIn("Absolute crank position is deliberately unused", self.code)

    def test_transient_imu_failure_does_not_force_zero_cadence(self):
        invalid = self.code[self.code.index("if (!sample.valid)"):
                            self.code.index("constexpr float kMotionThresholdDps")]
        self.assertIn("cadence_timeout_seconds", invalid)
        self.assertIn("latest_.moving = latest_.rpm > 0.1F", invalid)
        self.assertNotIn("latest_.moving = false", invalid)

    def test_ride_logging_has_a_single_setting_endpoint(self):
        server = (ROOT / "src" / "setup_wifi.cpp").read_text()
        self.assertIn("/api/ride-logging", server)
        self.assertIn("candidate.ride_detection_enabled = enabled == \"1\"", server)
        handler = server[server.index("rideLoggingHandler"):server.index("saveHandler")]
        self.assertNotIn("candidate.operating_mode", handler)
        self.assertNotIn("candidate.mqtt_", handler)

    def test_power_is_integrated_over_completed_revolutions(self):
        cadence_header = (ROOT / "src" / "cadence_estimator.h").read_text()
        power = (ROOT / "src" / "power_estimator.cpp").read_text()
        self.assertIn("forward_delta_radians", cadence_header)
        self.assertIn("revolution_completed", cadence_header)
        self.assertIn("revolution_work_joules_", power)
        self.assertIn("std::max(0.0F, latest_.torque_nm)", power)
        self.assertIn("revolution_work_joules_ / duration_seconds", power)
        self.assertNotIn("if (latest_.torque_nm < 0.0F)", power)

    def test_normal_sleep_waits_for_ride_save_and_report(self):
        main = (ROOT / "src" / "main.cpp").read_text()
        self.assertIn("ride_finalize_pending = g_ride_log.active()", main)
        self.assertIn("g_pending_report_reason != openwatts::ReportReason::None", main)
        self.assertIn("!ride_finalize_pending && !report_pending", main)


if __name__ == "__main__":
    unittest.main()
