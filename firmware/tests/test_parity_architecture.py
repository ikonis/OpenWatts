import math
import pathlib
import statistics
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


def normalize_angle(value):
    value = math.fmod(value, 360.0)
    return value + 360.0 if value < 0 else value


class RotationTraceModel:
    """Host-side contract test; not a claim that board angle tracking is valid."""

    def __init__(self):
        self.reset()

    def reset(self):
        self.last_angle = None
        self.revolutions = 0
        self.post_wake_samples = 0

    def update(self, angle, valid=True, forward=True):
        if not valid:
            return False
        self.post_wake_samples += 1
        angle = normalize_angle(angle)
        crossed = (
            forward
            and self.post_wake_samples >= 3
            and self.last_angle is not None
            and self.last_angle > 300
            and angle < 60
        )
        if crossed:
            self.revolutions += 1
        self.last_angle = angle
        return crossed


class ParityArchitectureTests(unittest.TestCase):
    def test_obsolete_runtime_prototype_is_absent(self):
        self.assertFalse((SRC / "runtime.h").exists())
        self.assertFalse((SRC / "runtime.cpp").exists())
        cmake = (SRC / "CMakeLists.txt").read_text()
        self.assertNotIn("runtime.cpp", cmake)

    def test_signed_cps_encoding_has_explicit_safe_boundary(self):
        text = (SRC / "ble_cycling_power.cpp").read_text()
        self.assertIn("const int16_t safe_power", text)
        self.assertIn("static_cast<uint16_t>(safe_power)", text)
        self.assertNotIn("sample.power_watts & 0xFF", text)

    def test_bad_samples_are_rejected_before_filter(self):
        text = (SRC / "power_estimator.cpp").read_text()
        reject_at = text.index("if (!hx711_ready)")
        filter_at = text.index("median_window_[median_index_]")
        self.assertLess(reject_at, filter_at)
        for reason in ("NegativeTorque", "InvalidCadence", "NonFinitePower", "AboveMaximum"):
            self.assertIn(reason, text)

    def test_filter_contract_median_five_then_ema(self):
        values = [100.0, 101.0, 1000.0, 99.0, 100.0]
        robust = statistics.median(values)
        self.assertEqual(robust, 100.0)
        seeded = robust
        next_value = seeded + 0.35 * (120.0 - seeded)
        self.assertAlmostEqual(next_value, 107.0)

    def test_angle_normalization(self):
        self.assertEqual(normalize_angle(360), 0)
        self.assertEqual(normalize_angle(-1), 359)
        self.assertEqual(normalize_angle(721), 1)

    def test_wake_event_is_not_a_revolution(self):
        tracker = RotationTraceModel()
        self.assertFalse(tracker.update(350))
        self.assertFalse(tracker.update(10))
        self.assertEqual(tracker.revolutions, 0)

    def test_forward_wrap_after_post_wake_qualification(self):
        tracker = RotationTraceModel()
        tracker.update(180)
        tracker.update(350)
        self.assertTrue(tracker.update(5))
        self.assertEqual(tracker.revolutions, 1)

    def test_reverse_wrap_is_not_forward_revolution(self):
        tracker = RotationTraceModel()
        tracker.update(180)
        tracker.update(10)
        self.assertFalse(tracker.update(350, forward=False))

    def test_missing_and_stale_imu_are_explicit(self):
        header = (SRC / "cadence_estimator.h").read_text()
        implementation = (SRC / "cadence_estimator.cpp").read_text()
        self.assertIn("ImuInvalidTimeout", header)
        self.assertIn("imu_invalid_reads", header)
        self.assertIn("cadence_timeout_seconds", implementation)

    def test_obsolete_rotation_prototype_is_absent(self):
        self.assertFalse((SRC / "rotation.h").exists())
        self.assertFalse((SRC / "rotation.cpp").exists())
        cmake = (SRC / "CMakeLists.txt").read_text()
        self.assertNotIn("rotation.cpp", cmake)

    def test_rotation_power_is_disabled_by_default(self):
        text = (SRC / "config.h").read_text()
        self.assertIn("reserved_rotation_aware_power_enabled = false", text)

    def test_nvs_migration_preserves_prefix(self):
        text = (SRC / "settings_storage.cpp").read_text()
        self.assertIn("DeviceConfig migrated{}", text)
        self.assertNotIn("size != sizeof(config)", text)

    def test_hardware_map_remains_openwatts_specific(self):
        text = (SRC / "board.h").read_text()
        self.assertIn("kImuInt = GPIO_NUM_10", text)
        self.assertIn("kHx711Dout = GPIO_NUM_0", text)
        self.assertNotIn("GPIO_NUM_23", text)

    def test_configuration_contract_is_persistent_and_transactional(self):
        config = (SRC / "config.h").read_text()
        storage = (SRC / "settings_storage.cpp").read_text()
        web = (SRC / "setup_wifi.cpp").read_text()
        self.assertIn("static constexpr uint32_t kVersion = 13", config)
        self.assertIn("OperatingMode operating_mode", config)
        for field in (
            "debug_logging_enabled",
            "auto_ride_zero_enabled",
            "ride_detection_enabled",
            "minimum_ride_duration_seconds",
            "cadence_timeout_seconds",
            "ble_advertising_power_dbm",
            "reserved_legacy_ble_auto_advertise_enabled",
        ):
            self.assertIn(field, config)
        self.assertIn("DeviceConfig candidate = current", web)
        self.assertIn("storage()->save(candidate)", web)
        self.assertIn("*g_portal->mutableConfig() = candidate", web)
        self.assertIn("minimum_ride_duration_seconds", storage)

    def test_settings_ui_only_enables_consumed_controls(self):
        ui = (SRC / "web_ui.cpp").read_text()
        for active in (
            "name=ble_name",
            "name=ble_advertising_power_dbm",
            "name=power_filter_alpha",
            "name=maximum_valid_power_watts",
            "name=ride_diagnostics",
            "name=auto_ride_zero",
            "name=ride_detection",
            "name=imu_wake_threshold",
            "name=cadence_timeout_seconds",
            "name=debug_logging",
        ):
            self.assertIn(active, ui)
        self.assertNotIn("id=rotationAware", ui)
        self.assertIn("id=rideZeroUnit", ui)
        self.assertIn("id=minRideUnit", ui)
        self.assertIn("id=cadenceTimeoutUnit", ui)

    def test_obsolete_bench_sleep_route_is_absent(self):
        server = (SRC / "setup_wifi.cpp").read_text()
        main = (SRC / "main.cpp").read_text()
        self.assertNotIn('"/bench"', server)
        self.assertNotIn("consumeBenchLightSleepRequest", main)

    def test_ride_diagnostics_controls_runtime_logging(self):
        main = (SRC / "main.cpp").read_text()
        self.assertIn("if (g_config.ride_diagnostics_enabled)", main)

    def test_operating_mode_is_the_user_policy_boundary(self):
        policy = (SRC / "operating_mode.cpp").read_text()
        main = (SRC / "main.cpp").read_text()
        web = (SRC / "web_ui.cpp").read_text()
        server = (SRC / "setup_wifi.cpp").read_text()
        self.assertIn("OperatingMode::Maintenance", policy)
        self.assertIn("usb_present ? 5U : 30U", policy)
        self.assertIn("OperatingPolicy::permitsInactivitySleep", main)
        self.assertIn("name=operating_mode", web)
        self.assertIn("modebadge", web)
        self.assertIn("switchOperatingMode('normal')", web)
        self.assertIn("switchOperatingMode('maintenance')", web)
        self.assertIn('uri = "/api/operating-mode"', server)
        self.assertIn("operatingModeHandler", server)
        self.assertNotIn("IMU Tuning Session", web)


if __name__ == "__main__":
    unittest.main()
