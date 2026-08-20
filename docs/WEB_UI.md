# Web Interface

Firmware 1.1.1 presents one consistent responsive product interface across
desktop and mobile. The OpenWatts logo returns to Status and the shared header
contains Status, Ride, Settings, Calibration, OTA Update, and Diagnostics.

- Status: ordinary device, battery, connection and readiness information.
- Settings: authoritative NVS-backed controls; unsafe items remain disabled.
- Calibration: strain assessment, staged Bench Calibration, Manual Tare,
  direction reversal, verification and confirmed reset.
- OTA Update: binary upload and automatic reboot on USB, or in Maintenance Mode with a valid battery at or above 3.75 V.
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
- USB OTA, plus guarded Maintenance Mode battery OTA at or above 3.75 V

## Deliberately disabled or display-only

Rotation-aware sector weighting remains disabled because the product does not
need an absolute crank-angle model. Diagnostics provisional IMU output remains
observation only and cannot feed production power. Last Ride is stored locally
and published through the existing retained MQTT payload when pending.

There is no general Reboot or Factory Reset action in the current WebUI.
# Live Ride dashboard

`/ride` is a phone-first, read-only dashboard for active rides. OpenWatts supplies
its authoritative power, cadence, modeled speed, candidate/qualified ride time
and distance, and battery state through the existing `/status` snapshot.

The browser independently polls
`http://smartspin2k.local/runtimeConfigJSON` approximately once per second for
measured/target power, cadence, heart rate, resistance,
logical shifter position, and FTMS control mode. OpenWatts does not proxy or
persist this SmartSpin2K data. A missing or malformed SS2K value displays as
zero or unavailable and cannot affect BLE CPS or ride recording.

In Normal Mode, removing USB keeps the already-running dashboard available
through the possible ride and its normal post-ride reporting/sleep boundary.
After a motion-only wake, Wi-Fi starts once cadence has formed a ride candidate
(normally after 10 seconds) rather than waiting for full ride qualification.
Reconnecting USB while a qualified ride is active finalizes it as soon as
cadence is zero, persists it, and queues its normal MQTT report. Network startup
is attempted once and failure never creates a sampling-loop retry. Maintenance
Mode behavior is unchanged.
