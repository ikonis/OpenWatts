# First ride validation

Cadence and power have passed practical installed-crank ride validation. Use
this procedure as a regression check after calibration, mounting, sensor, BLE,
or cadence changes.

During the first short controlled ride, record:

- BLE pairing/reconnection and Cycling Power Service compatibility;
- OpenWatts cadence versus a trusted reference at steady 60 and 90 RPM;
- false revolutions while stationary or during vibration;
- torque return-to-zero and drift;
- bounded, non-negative power and over-limit rejection;
- start/stop response and filter reacquisition;
- behavior after BLE disconnect, inactivity sleep, and motion wake;
- reset/watchdog/brownout evidence from serial or the Diagnostics page.

When enabled, ride history begins only from calibrated, valid samples. Cadence
must remain active for ten seconds and accumulate the configured minimum moving
duration. Five minutes at zero cadence completes and persists the latest ride.
Compare the saved Last Ride with SmartSpin2K/MyWhoosh as an external regression
check.
