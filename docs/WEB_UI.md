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
