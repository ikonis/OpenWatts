# OpenWatts firmware

OpenWatts is an ESP32-C3 cycling power meter using an HX711 strain channel and
an LSM6DS3 IMU. This tree is the independent OpenWatts implementation of the
runtime and policy boundaries validated in ikoniWatts.

## Hardware boundary

The pin map in `src/board.h` is authoritative and matches the shipped PCB:
HX711 DOUT/SCK GPIO0/1, LED GPIO3, battery ADC GPIO4, charge status GPIO5,
I2C SDA/SCL GPIO6/7, USB-present GPIO8, boot GPIO9, IMU INT GPIO10, and native
USB GPIO18/19. GPIO10 is not an ESP32-C3 RTC GPIO, so the fitted IMU can wake
light sleep but not deep sleep.

The LSM6DS3 provider reports missing/read-failed states explicitly. Current
cadence is a bring-up threshold-crossing implementation and is not a validated
product cadence algorithm. Rotation-aware power is scaffolded and disabled.

## Operating Mode and runtime model

The persistent user-facing Operating Mode is either **Normal** or
**Maintenance**. Normal maximizes battery life and limits Wi-Fi to USB or a
policy-required battery report. Maintenance keeps Wi-Fi/WebUI available on
battery and permits calibration, Manual Tare, direction reversal, diagnostics,
raw data, and continuous provisional IMU tuning. The Settings segmented control
saves and applies the mode immediately without the general Save Settings button.

The intended product boundary is four exclusive modes:

- `NORMAL`: IMU, cadence, HX711 and BLE CPS riding services.
- `USB`: maintenance Wi-Fi, web configuration, calibration and OTA.
- `TIMER_DECISION`: battery ADC and report policy only.
- `REPORT`: battery ADC, Wi-Fi and MQTT only.

The current parity checkpoint adds the shared policy and runtime contracts while
retaining the existing light-sleep riding loop until board validation permits
the restart-based transitions to be enabled safely. See
`../docs/RUNTIME_ARCHITECTURE.md`.

## Power pipeline

The active estimator remains time-domain and replaceable. Fresh valid samples
are checked before filtering, passed through median-of-five and EMA (`alpha`
default 0.35), then clamped at the CPS boundary. Negative, stale, non-finite,
implausible cadence, and over-limit power are rejected. The default believable
power limit is 2000 W and may be configured from 500–5000 W.

BLE CPS instantaneous power is explicitly encoded as signed little-endian
16-bit. Invalid or negative samples are emitted as zero and never wrap to 65536.

## First-flash maintenance and Home Assistant bring-up

With USB present, OpenWatts starts the `OpenWatts-Setup` access point at
`192.168.4.1`. The setup page stores Wi-Fi credentials plus MQTT host, port,
topic, and reporting enablement in NVS. Defaults are `192.168.1.28:1883` and
`openwatts/battery`.

Once Wi-Fi credentials are saved, the firmware keeps the setup AP available
and joins the configured network as a station. MQTT reporting publishes a
retained battery state and Home Assistant discovery for battery voltage,
estimated battery, battery state, firmware version, and device health.

Maintenance Mode replaces the former `Keep Wi-Fi on without USB` override.
Existing installations migrate that setting automatically during OTA. It
deliberately increases battery consumption and does not alter the
C3/IMU ride logic.

This is a bring-up port, not evidence that the timer/report runtime has been
physically validated on OpenWatts. Native USB or the J2 3.3 V UART header is
required for the first flash.

## Build and test

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e esp32c3
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e esp32c3_diagnostic
python -m unittest discover firmware/tests
```

Tooling is pinned to Espressif32 6.11.0, ESP-IDF 5.4.1, and RISC-V GCC
14.2.0+20241119.

Do not flash this parity checkpoint until the actual PCB pin/ADC/IMU bench
validation in `../docs/VALIDATION.md` has been completed.

## Installation readiness

The Maintenance WebUI now provides complete multi-sample Bench Calibration, Manual
Tare, direction reversal, verification, and explicit calibration reset.
Permanent calibration zero/scale are stored separately from runtime zero.

Diagnostics includes continuous provisional IMU tuning while Maintenance Mode
is selected. Its angle, cadence and confidence never feed production power. Rotation-aware power
and Automatic Ride Zero remain disabled pending crank-mounted validation. See
`../docs/INSTALLATION_CHECKLIST.md` and `../docs/IMU_TUNING.md`.
