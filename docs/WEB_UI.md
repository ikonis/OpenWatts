# Web Interface

- Status: ordinary device, battery, connection and readiness information.
- Settings: authoritative NVS-backed controls; unsafe items remain disabled.
- Calibration: strain assessment, staged Bench Calibration, Manual Tare,
  direction reversal, verification and confirmed reset.
- OTA Update: USB-only binary upload and automatic reboot.
- Diagnostics: installation evidence, continuous Maintenance-mode IMU tuning,
  raw sensor data, and checklist.

Settings begins with the first-class Operating Mode selector. Every page shows
the current Normal or Maintenance badge beside uptime. Selecting either mode
persists and applies it immediately; the general Save Settings button is only
for the remaining configuration fields.

Actions return inline success/failure messages. `/status` is authoritative.

## Enabled controls

- Operating Mode (immediate)
- Wi-Fi credentials (persisted; connection changes require restart)
- MQTT enable/broker/port/topic
- motion-wake sleep and idle delay (sleep-manager changes require restart)
- BLE device name (requires restart)
- BLE advertising power (requires restart)
- power smoothing and maximum believable power
- automatic Ride Zero and stationary delay (calibration-gated)
- ride detection, minimum ride duration, and persisted Last Ride summary
- cadence timeout/range and IMU wake/rotation thresholds
- debug logging
- ride diagnostic serial logging
- calibration, verification, Manual Tare, direction, reset
- USB-only OTA

## Deliberately disabled or display-only

Rotation-aware power remains disabled because the installed-crank angle model
has not been validated. Diagnostics provisional IMU output remains observation
only and cannot feed production power. Last Ride is stored locally; it is not
added to the battery MQTT payload.

There is no general Reboot or Factory Reset action in the current WebUI.
