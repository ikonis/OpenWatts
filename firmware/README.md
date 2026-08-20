# OpenWatts firmware

Firmware 1.1.1 is the first post-installation product milestone. The assembled
RevA has completed calibration, BLE/SmartSpin2K riding, cadence, ride-history,
battery, sleep/wake, WebUI, OTA, MQTT, and Home Assistant field validation.

OpenWatts is an ESP32-C3 cycling power meter using an HX711 strain channel and
an LSM6DS3 IMU. The firmware provides BLE Cycling Power, persistent calibration
and Ride Zero, ride history, battery-conscious sleep, USB/Maintenance Wi-Fi,
OTA, MQTT, and a responsive local WebUI.

## Hardware boundary

`src/board.h` is authoritative for the assembled RevA PCB. GPIO10 is not an
ESP32-C3 deep-sleep wake GPIO, so motion wake uses IMU-armed light sleep. The
firmware does not implement deep sleep.

## Operating modes

- **Normal** is the riding mode. BLE and sensors remain responsive; Wi-Fi is
  limited to USB and bounded reports; inactivity permits light sleep.
- **Maintenance** keeps Wi-Fi/WebUI and service tools available on battery and
  intentionally prevents inactivity sleep.

The mode applies immediately and persists across reboot.

## Measurement and ride behavior

- Cadence is derived from completed forward IMU rotations and published through
  BLE CPS crank-revolution data.
- Torque comes from the calibrated HX711 bridge on the **left crank**. Bench
  Calibration owns the permanent left-crank scale; Manual Tare and Automatic
  Ride Zero only change runtime zero.
- Power uses the authoritative cadence/torque pipeline, invalid-sample rejection,
  completed-revolution integration, and then doubles measured left-side power to
  estimate total rider power before median filtering and a lightweight EMA.
  This is the conventional single-sided-meter assumption of approximately equal
  left/right contribution. A real leg imbalance creates the same estimation
  error found in commercial single-sided meters.
- HX711 counts, zero offsets, calibration scale, and displayed torque remain
  actual left-crank measurements. Cadence is measured directly and is never
  doubled.
- A qualified completed ride is retained in NVS with power, cadence, work,
  revolutions, and reproducible road-model context.
- A completed ride is marked MQTT-pending. When ordinary Normal Mode sleep is
  due, firmware makes one bounded QoS 1 report attempt, then sleeps regardless
  of network success. Failed reports remain pending for a later opportunity.

Road estimates use the corrected total cycling-power value and SI internally.
Model version 1 applies 97% drivetrain
efficiency and solves flat-road rolling plus aerodynamic resistance. The WebUI
defaults to Imperial presentation; MQTT remains SI-native for Home Assistant.

## Wi-Fi, WebUI, and OTA

Saved credentials use station mode. Missing credentials or an explicit setup
request starts `OpenWatts-Setup` with wildcard DNS at `192.168.4.1`.

The WebUI provides Status, a phone-first live Ride dashboard, Settings,
Calibration, OTA Update, and Diagnostics.
OTA requires USB, or Maintenance Mode with a qualified battery above the safety
threshold. Settings use one NVS-backed `DeviceConfig`; display units never
change stored physical meaning.

MQTT uses retained plain TCP on `openwatts/battery`. New devices have no
broker configured; enter the deployment broker in Settings. Broker
authentication and TLS are not implemented.

## Diagnostics

Normal operation retains compact field diagnostics: sensor readiness/failures,
BLE delivery counters, reset/wake cause, calibration state, and battery health.
**Debug Logging** enables verbose runtime traces. **Ride diagnostics** adds
periodic engineering ride logs and should remain off for normal use.

## Build and test

```powershell
cd firmware
python -m unittest discover -s tests -p "test_*.py"
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e esp32c3 -e esp32c3_diagnostic
```

Tooling is pinned to Espressif32 6.11.0, ESP-IDF 5.4.1, and RISC-V GCC
14.2.0+20241119. Python tests are host-side contract tests; physical BLE,
sensor, sleep/wake, Wi-Fi, and OTA validation remains required for releases.

See `../docs/` for calibration, runtime, WebUI, Home Assistant, and validation
details.
