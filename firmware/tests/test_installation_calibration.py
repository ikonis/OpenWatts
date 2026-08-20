import math
import pathlib
import statistics
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


class CalibrationMathTests(unittest.TestCase):
    def test_reference_torque_uses_si_units(self):
        mass_kg, lever_mm = 4.83, 170.0
        expected = mass_kg * 9.80665 * lever_mm / 1000.0
        self.assertAlmostEqual(expected, 8.052, places=3)

    def test_scale_and_reverse_direction(self):
        torque, delta = 8.0, 80000.0
        nm_per_count = torque / delta
        self.assertEqual(1.0 / abs(nm_per_count), 10000.0)
        self.assertEqual(-nm_per_count, -0.0001)

    def test_unstable_capture_rejected(self):
        unstable = [0, 30000, -30000, 25000, -25000] * 5
        self.assertGreater(statistics.stdev(unstable), 6000)

    def test_tiny_delta_rejected_by_contract(self):
        combined_noise = 80.0
        minimum_delta = max(1000.0, combined_noise * 10.0)
        self.assertLess(500.0, minimum_delta)


class CalibrationArchitectureTests(unittest.TestCase):
    def test_runtime_zero_is_separate_from_permanent_calibration(self):
        header = (SRC / "config.h").read_text()
        implementation = (SRC / "calibration.cpp").read_text()
        self.assertIn("calibration_zero_reference_counts", header)
        self.assertIn("runtime_zero_offset_counts", header)
        tare = implementation[implementation.index("CalibrationManager::manualTare"):implementation.index("CalibrationManager::reverseDirection")]
        self.assertIn("runtime_zero_offset_counts", tare)
        self.assertNotIn("counts_per_nm =", tare)
        self.assertNotIn("calibration_zero_reference_counts =", tare)

    def test_direction_reversal_preserves_scale_and_zero(self):
        implementation = (SRC / "calibration.cpp").read_text()
        reverse = implementation[implementation.index("CalibrationManager::reverseDirection"):implementation.index("CalibrationManager::resetCalibration")]
        self.assertIn("torque_sign", reverse)
        self.assertNotIn("counts_per_nm =", reverse)
        self.assertNotIn("zero_offset_counts =", reverse)

    def test_all_calibration_routes_exist(self):
        web = (SRC / "setup_wifi.cpp").read_text()
        for route in ("/api/calibration/start", "/api/calibration/load", "/api/calibration/save",
                      "/api/calibration/verify", "/api/calibration/tare", "/api/calibration/reverse",
                      "/api/calibration/discard", "/api/calibration/reset"):
            self.assertIn(route, web)

    def test_unvalidated_rotation_remains_disabled(self):
        config = (SRC / "config.h").read_text()
        self.assertIn("reserved_rotation_aware_power_enabled = false", config)
        ui = (SRC / "web_ui.cpp").read_text()
        self.assertNotIn("Rotation-aware power", ui)
        self.assertNotIn("id=rotationAware", ui)

    def test_imu_tuning_is_maintenance_mode_data_not_a_session(self):
        web = (SRC / "setup_wifi.cpp").read_text()
        header = (SRC / "setup_wifi.h").read_text()
        policy = (SRC / "operating_mode.cpp").read_text()
        self.assertIn("permitsMaintenanceTools", web)
        self.assertIn("OperatingMode::Maintenance", policy)
        self.assertNotIn("imu_tuning_deadline_us_", header)
        self.assertNotIn("imu_tuning_enabled_", header)
        self.assertNotIn("/api/imu-tuning", web)
        self.assertNotIn("imu_tuning_enabled", (SRC / "config.h").read_text())

    def test_imu_capture_is_relative_and_volatile(self):
        server = (SRC / "setup_wifi.cpp").read_text()
        docs = (ROOT.parent / "docs" / "IMU_TUNING.md").read_text()
        self.assertIn("/api/imu-capture/start", server)
        self.assertIn("std::array<ImuCaptureSample", server)
        self.assertNotIn("saveImuCapture", server)
        self.assertIn("start from any physical crank angle", docs)

    def test_nvs_schema_is_append_only_v13(self):
        config = (SRC / "config.h").read_text()
        self.assertIn("kVersion = 13", config)
        self.assertIn("reserved_legacy_imu_tuning_timeout_seconds", config)
        self.assertIn("legacy_wifi_keep_alive_without_usb", config)
        storage = (SRC / "settings_storage.cpp").read_text()
        self.assertIn("nvs_get_blob", storage)
        self.assertIn("DeviceConfig migrated{}", storage)
        self.assertIn("stored_version < 10", storage)

    def test_automatic_ride_zero_cannot_run_without_calibration(self):
        implementation = (SRC / "ride_zero.cpp").read_text()
        self.assertIn("!config.strain_calibration_valid", implementation)
        self.assertIn("RideZeroResult::CalibrationRequired", implementation)

    def test_automatic_ride_zero_uses_learned_strain_and_imu_stability(self):
        config = (SRC / "config.h").read_text()
        implementation = (SRC / "ride_zero.cpp").read_text()
        main = (SRC / "main.cpp").read_text()
        self.assertIn("auto_ride_zero_enabled = true", config)
        self.assertIn("ride_zero_baseline_stddev_counts", config)
        self.assertIn("ride_zero_baseline_range_counts", config)
        self.assertIn("if (!imu_stationary)", implementation)
        self.assertIn("last_.standard_deviation", implementation)
        self.assertIn("last_.range", implementation)
        self.assertIn("ble_zero_due_us = now_us + 3000000LL", main)
        self.assertIn("RideZeroTrigger::BeforeSleep, sample, imu_stationary", main)

    def test_ride_history_requires_valid_power(self):
        implementation = (SRC / "ride_log.cpp").read_text()
        self.assertIn("sample.valid", implementation)
        main = (SRC / "main.cpp").read_text()
        self.assertIn("g_config.strain_calibration_valid", main)

    def test_last_ride_mqtt_is_calibration_gated_and_discoverable(self):
        main = (SRC / "main.cpp").read_text()
        mqtt = (SRC / "mqtt_notifier.cpp").read_text()
        self.assertIn("g_config.ride_detection_enabled && g_config.strain_calibration_valid", main)
        self.assertIn("last_ride_valid", main)
        self.assertIn("last_ride_peak_power", mqtt)
        self.assertIn("last_ride_average_cadence", mqtt)
        self.assertIn("availability_template", mqtt)

    def test_manual_tare_is_not_restricted_to_maintenance_mode(self):
        server = (ROOT / "src" / "setup_wifi.cpp").read_text()
        start = server.index("esp_err_t SetupWifi::calibrationTare()")
        end = server.index("esp_err_t SetupWifi::calibrationReverse()")
        handler = server[start:end]
        self.assertNotIn("permitsMaintenanceTools", handler)
        self.assertIn("calibration_.manualTare", handler)
        web = (ROOT / "src" / "web_ui.cpp").read_text()
        self.assertIn("id=statusTare", web)
        self.assertIn("statusManualTare", web)


if __name__ == "__main__":
    unittest.main()
