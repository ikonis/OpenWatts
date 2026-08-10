# OpenWatts firmware

OpenWatts is an ESP32-C3 cycling power-meter firmware using an HX711 strain
channel and LSM6DS3 IMU. The current `0.2.0-parity` build is a pre-install
baseline: maintenance, calibration, BLE CPS, battery, MQTT, OTA, and sleep
foundations exist, but cadence and installed strain behavior are not yet
product-validated.

See [`../docs/IMPLEMENTATION_STATUS.md`](../docs/IMPLEMENTATION_STATUS.md) for
the authoritative implemented/partial/missing/superseded matrix.

## Hardware boundary

`src/board.h` is authoritative for the shipped PCB:

- HX711 DOUT/SCK: GPIO0/1
- awake LED: GPIO3
- battery ADC: GPIO4
- charge status: GPIO5
- I2C SDA/SCL: GPIO6/7
- USB present: GPIO8
- boot: GPIO9
- IMU INT1: GPIO10
- native USB: GPIO18/19

GPIO10 is not an ESP32-C3 deep-sleep wake GPIO. OpenWatts therefore uses IMU-
armed light sleep for motion wake. Deep sleep is not implemented.

## Operating Mode

The persistent user-facing mode is exactly **Normal** or **Maintenance**.

- Normal limits Wi-Fi to USB or a required battery report and allows inactivity
  light sleep.
- Maintenance keeps Wi-Fi/WebUI active on battery, enables calibration/raw/IMU
  tools, publishes MQTT on a fixed maintenance cadence, and prevents inactivity
  sleep.

The segmented control applies and saves immediately. Schema 9 and older migrate
the former keep-Wi-Fi setting to the corresponding schema 10 mode.

## What is usable now

- Wi-Fi station mode and recovery/setup AP at `192.168.4.1`
- responsive WebUI with live status, settings, calibration, OTA, diagnostics
- USB-only OTA with image/partition validation and automatic reboot
- NVS persistence and append-only migration
- battery voltage/display estimate, voltage states, and MQTT reporting
- Home Assistant MQTT discovery for five battery/device sensors
- IMU motion wake, USB wake, and timer wake from light sleep
- HX711 reading/filtering/readiness/failure handling
- guided Bench Calibration, verification, Manual Tare, direction reversal/reset
- signed-safe BLE Cycling Power Service and crank-revolution fields
- invalid-sample rejection plus median-five/EMA power smoothing

## Not ready for product riding

- Cadence is a gyro-Z threshold-crossing bring-up stub. It has not been tuned to
  the installed crank and can count the wrong motion.
- Provisional Maintenance IMU angle/cadence/confidence is display-only and does
  not feed BLE or power.
- Rotation-aware power, Automatic Ride Zero, ride detection/history, BLE radio
  controls, debug logging control, deep sleep, and battery protection shutdown
  are not implemented.
- Permanent strain calibration has not been validated on the installed bridge.
- Several persisted compatibility/scaffolding fields are unused; the status
  document lists each one.

## Runtime truth

Current firmware uses one initialized application loop and resumes after light
sleep. A timer wake takes a direct battery-decision branch and can return to
sleep without the normal sampling body. The compiled `RuntimeMode` names are
not a dispatcher, and OpenWatts does not currently use ikoniWatts' restart-based
exclusive timer/report runtimes.

## Wi-Fi and MQTT

If credentials are missing, setup is explicitly requested, or the setup flag is
set, firmware starts the open `OpenWatts-Setup` AP with wildcard DNS at
`http://192.168.4.1/`. With saved credentials and no setup request, it starts
station mode only; it does not keep the AP running.

Default MQTT is plain TCP at `192.168.1.28:1883`, topic
`openwatts/battery`. There is no MQTT authentication or TLS configuration.
Messages are retained. Firmware report history is RAM-only.

## Build and test

```powershell
cd firmware
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e esp32c3
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e esp32c3_diagnostic
py -3 tests/test_parity_architecture.py
py -3 tests/test_installation_calibration.py
```

Tooling is pinned to Espressif32 6.11.0, ESP-IDF 5.4.1, and RISC-V GCC
14.2.0+20241119. The diagnostic target defines
`OPENWATTS_DIAGNOSTIC_BUILD=1`, but no current code consumes that macro.

The Python tests are host-side contract/source tests, not hardware tests.
