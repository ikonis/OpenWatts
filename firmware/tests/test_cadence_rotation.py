from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class CadenceRotationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.code = (ROOT / "src" / "cadence_estimator.cpp").read_text()

    def test_installed_forward_direction_uses_negative_gyro_z(self):
        self.assertIn("forward_velocity_dps = -corrected_z", self.code)

    def test_active_gyro_range_covers_real_crank_velocity_peaks(self):
        imu = (ROOT / "src" / "imu_lsm6ds3.cpp").read_text(encoding="utf-8")
        self.assertIn("writeReg(kRegCtrl2G, 0x4C)", imu)
        self.assertIn("gyro_lsb_per_dps = 70.0F / 1000.0F", imu)
        config = (ROOT / "src" / "config.h").read_text(encoding="utf-8")
        self.assertIn("imu_gyro_range_dps = 2000", config)

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
        self.assertIn("ride_report_pending = g_ride_log.lastRide().valid", main)
        self.assertIn("mqtt_publish_pending", main)
        self.assertIn("bounded pre-sleep Last Ride report started", main)
        self.assertIn("kSleepReportTimeoutUs", main)
        self.assertNotIn("!ride_finalize_pending && !report_pending", main)

    def test_last_ride_publish_intent_is_durable_and_acknowledged(self):
        ride_h = (ROOT / "src" / "ride_log.h").read_text()
        ride_cpp = (ROOT / "src" / "ride_log.cpp").read_text()
        storage = (ROOT / "src" / "settings_storage.cpp").read_text()
        main = (ROOT / "src" / "main.cpp").read_text()
        self.assertIn("mqtt_publish_pending = false", ride_h)
        self.assertIn("completed.mqtt_publish_pending = true", ride_cpp)
        self.assertIn("markMqttPublished", main)
        self.assertIn("saveLastRide(g_ride_log.lastRide())", main)
        self.assertIn("LastRideSummaryV2", storage)
        self.assertIn("summary.mqtt_publish_pending = false", storage)

    def test_failed_pre_sleep_report_cannot_block_sleep_forever(self):
        main = (ROOT / "src" / "main.cpp").read_text()
        self.assertIn("now_us - g_mqtt_started_us < kSleepReportTimeoutUs", main)
        self.assertIn("pre-sleep MQTT deadline expired", main)
        self.assertIn("retaining pending ride", main)
        self.assertIn("g_power_manager.enterSleep", main)


if __name__ == "__main__":
    unittest.main()
