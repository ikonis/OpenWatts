# First ride validation

Do not treat the current cadence output as production-ready. Before riding,
complete the installed bridge and IMU checks in `VALIDATION.md`.

During the first short controlled ride, record:

- BLE pairing/reconnection and Cycling Power Service compatibility;
- OpenWatts cadence versus a trusted reference at steady 60 and 90 RPM;
- false revolutions while stationary or during vibration;
- torque return-to-zero and drift;
- bounded, non-negative power and over-limit rejection;
- start/stop response and filter reacquisition;
- behavior after BLE disconnect, inactivity sleep, and motion wake;
- reset/watchdog/brownout evidence from serial or the Diagnostics page.

The current firmware has no ride lifecycle or last-ride recorder. Preserve
evidence externally. The Last Ride WebUI card does not store data.
