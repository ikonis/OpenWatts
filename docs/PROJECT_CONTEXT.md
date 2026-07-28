# OpenWatts project context

OpenWatts is the ESP32-C3/LSM6DS3 successor to the ESP32-C6/Hall-based
ikoniWatts prototype. Shared behavior should remain mirrored at the policy
level, not copied at the GPIO or sensor-driver level.

The assembled PCB is fixed. Firmware must derive pins from `board.h` and must
not invent hardware changes. Saved Wi-Fi credentials and strain calibration
are product data and must survive schema upgrades. Runtime tare is disabled
unless it is separately proven safe.

Current status:

- BLE CPS, HX711, board IO and LSM6DS3 access exist.
- Shared runtime, battery/report policy, power validation and rotation contracts
  exist.
- IMU angle, direction and revolution accuracy are not physically validated.
- Rotation-aware power is disabled and must remain so until validation.
- Maintenance/AP behavior is bring-up quality; full product STA/MQTT/OTA UI is
  a deferred hardware-validation phase, not falsely advertised as complete.
