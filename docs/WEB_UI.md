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
- power smoothing and maximum believable power
- ride diagnostic serial logging
- calibration, verification, Manual Tare, direction, reset
- USB-only OTA

## Deliberately disabled or display-only

BLE advertising power/behavior, Automatic Ride Zero, ride detection, minimum
ride duration, cadence timeout, motion sensitivity, rotation threshold,
rotation-aware power, and debug logging are not connected to production
algorithms. Some values are visible from NVS scaffolding but cannot be edited.

The Last Ride card is a placeholder; no ride lifecycle, summary storage, or
MQTT ride publication exists. Diagnostics provisional IMU output is for
observation only and cannot tune or change the production cadence stub.

There is no general Reboot or Factory Reset action in the current WebUI.
