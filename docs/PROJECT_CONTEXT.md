# OpenWatts project context

OpenWatts is the ESP32-C3/LSM6DS3 successor to the ESP32-C6/Hall-based
earlier proof-of-concept firmware. The assembled PCB is fixed. Firmware must use `board.h`
and must not invent PCB changes.

The current repository is a **pre-install baseline**, not a completed power
meter. Hardware access, BLE CPS, WebUI, OTA, NVS, battery/MQTT foundations,
light sleep/wake, HX711 handling, and calibration workflows are implemented.
Production cadence and installed strain behavior remain unvalidated.

## Current product policy

- Normal Mode is the battery-conscious riding policy.
- Maintenance Mode is the deliberate installation/calibration/diagnostics
  policy and keeps Wi-Fi available on battery.
- Bench Calibration and runtime Manual Tare are separate persisted concepts.
- Provisional IMU data is isolated from production BLE/power output.
- Rotation-aware power remains disabled. Automatic Ride Zero and ride history
  are implemented but cannot operate until Bench Calibration is valid.

## Important current limitations

- Cadence uses a fixed gyro-Z threshold crossing, not a validated crank model.
- Main execution is a resumed light-sleep loop, not restart-based exclusive
  runtime dispatch.
- Critical/protection battery states have no shutdown action yet.
- Some schema fields are scaffolding only; see `IMPLEMENTATION_STATUS.md`.
- MQTT is unauthenticated plain TCP and its device-health field is hard-coded.
- The diagnostic build currently has no behavioral instrumentation beyond its
  compile definition.

Saved Wi-Fi credentials, Operating Mode, and calibration are product data and
must survive OTA/schema upgrades. New persisted fields must remain append-only.

Hardware-independent behavior belongs in policy/service modules; ESP32-C3,
LSM6DS3, USB, battery, and HX711 behavior remains behind OpenWatts drivers.
