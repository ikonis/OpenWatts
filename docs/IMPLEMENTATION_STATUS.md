# OpenWatts implementation status

This document is an audit of the source at the pre-install baseline. It is not
a roadmap disguised as current behavior. A feature is listed as implemented
only when the production code calls it.

Firmware version: `0.2.0-parity`  
NVS schema: `10`  
Targets: `esp32c3` and `esp32c3_diagnostic`

## Implemented and active

| Area | Current behavior |
|---|---|
| Operating Mode | Persistent `Normal` or `Maintenance`. The Settings segmented control saves and applies the mode immediately. Older `Keep Wi-Fi on without USB` state migrates to Maintenance. |
| Normal Wi-Fi policy | Wi-Fi/WebUI are available with USB. Battery reporting may temporarily start Wi-Fi. Normal battery operation may enter light sleep. |
| Maintenance policy | Wi-Fi/WebUI remain active after USB removal, maintenance tools are allowed, inactivity sleep is disabled, and MQTT is evaluated every 5 seconds on USB or 30 seconds on battery. |
| Wi-Fi provisioning | With no credentials, or when setup is explicitly requested, the open `OpenWatts-Setup` AP and wildcard DNS serve the portal at `192.168.4.1`. With saved credentials and no setup request, firmware uses station mode only. |
| NVS | One append-only configuration blob stores credentials, mode, calibration, and supported settings. Shorter old blobs load into current defaults and schema 9 mode state migrates to schema 10. |
| WebUI | Status, Settings, Calibration, OTA Update, and Diagnostics pages share one responsive header and live `/status` data. |
| OTA | Raw ESP application `.bin` upload, image magic/size/partition validation, USB-only enforcement, activation, and automatic reboot. There is no signature verification. |
| Battery measurement | Fifteen calibrated ADC samples are trimmed and averaged, then scaled. Voltage is authoritative. Display percentage uses a fixed 1S LiPo curve ending at 4.16 V plus EMA/hysteresis for display stability. |
| Battery state | Voltage classification for Healthy, Charge Soon, Charge Now, Critical, Protection, and Invalid. A state needs the configured number of matching readings (default two) before qualification. |
| MQTT reporting | Plain TCP MQTT publishes a retained JSON battery message and retained Home Assistant discovery for voltage, estimated percent, battery state, firmware, and device health. Normal reports use boot/state/delta/retry/heartbeat/USB reasons. |
| Light sleep | In Normal Mode, after BLE disconnect and inactivity, BLE/Wi-Fi/HX711 stop, the LSM6DS3 enters low-power wake mode, and light sleep arms IMU, USB, and timer wake sources. |
| IMU wake | LSM6DS3 INT1 wakes the ESP32-C3 from light sleep. Motion/USB wake restores active IMU, HX711, and BLE advertising. |
| HX711 | 24-bit signed reads, timeout/failure accounting, readiness confidence, EMA counts, residual-noise estimate, and power-down/resume around light sleep. Missing data does not crash the loop. |
| Bench Calibration | Three-second unloaded/loaded captures, stability and signal checks, known-mass torque calculation, review/save, verification capture, persistent scale/zero/direction, and explicit reset. |
| Manual Tare | Maintenance-only, calibration-required update of runtime zero. It does not alter permanent calibration scale or reference zero. |
| Reverse Direction | Maintenance-only persistent torque-sign reversal without changing scale or zero. |
| Power validation | Rejects missing HX711, missing calibration, invalid/negative torque, invalid/stale cadence, non-finite values, and over-limit power before filtering. |
| Power smoothing | Median of up to five accepted power samples followed by configurable EMA. Invalid samples do not enter the filter. |
| BLE CPS | NimBLE Cycling Power Service advertises the configured name and cycling-power appearance. Measurement notifications contain signed-safe instantaneous power plus crank revolution data. Invalid/negative power is encoded as zero. |
| Boot self-test | IMU/HX711 self-test runs on initial/default configuration or when requested by the boot button and is exposed through serial/BLE diagnostics text. |

## Partial or bring-up-only

| Area | What exists | What is not complete |
|---|---|---|
| Cadence | Counts a revolution when gyro Z crosses a fixed threshold, rearms below 25% of the threshold, and derives RPM from consecutive crossings. | Axis, orientation, filtering, direction, false-motion rejection, cadence timeout, and installed-crank accuracy are not validated. This is not a production cadence algorithm. |
| Provisional IMU view | Maintenance computes dominant-axis velocity, integrated absolute angle, tentative cadence/revolutions, and a simple confidence value for the Diagnostics page. | It does not calibrate or tune the production algorithm, save results, or feed BLE/power/calibration. |
| Timer battery path | A timer wake stays in the existing process, reads battery, decides whether to report, and otherwise returns to light sleep without executing the main sampling body. | It is not the restart-based `TIMER_DECISION`/`REPORT` architecture used by ikoniWatts. Runtime enums are not used for dispatch. |
| MQTT reliability | Publish success updates in-RAM report history; failure sets an in-RAM retry flag. | History does not survive reboot, there is no username/password/TLS setting, and the WebUI does not expose connection/result state. |
| Device health | The Status page derives a basic IMU/HX711 readiness label. | MQTT currently publishes `device_health` as hard-coded `Healthy`, even when sensors are not healthy. |
| Charging status | GPIO5 active-low status is shown as charging/not charging. | It does not provide charge current, time remaining, or a separate full/fault state. |
| General Settings application | Values persist transactionally and reload into the page. Operating Mode applies immediately. Power parameters are consumed by the main loop. | BLE name, Wi-Fi credentials, and sleep-manager configuration require reboot/reinitialization. The UI uses one general “reboot required” response rather than per-setting application status. |
| Diagnostic target | `esp32c3_diagnostic` builds with `OPENWATTS_DIAGNOSTIC_BUILD=1`. | No current source checks that macro, so behavior differs only in the define/build size. |

## Present in NVS or source but not implemented

These fields/contracts are retained as scaffolding or compatibility data. They
must not be described as working controls.

- `deep_sleep_enabled`: not read by runtime code. Normal operation uses light
  sleep because GPIO10 is not an ESP32-C3 deep-sleep wake pin.
- `battery_hysteresis_voltage`: stored and sanitized, but battery state
  qualification does not apply voltage hysteresis.
- `usb_voltage_publish_delta`: stored and sanitized, but report policy always
  uses `battery_report_voltage_delta`.
- MQTT percentage thresholds: stored and sanitized but report decisions use
  voltage states, not these percentages.
- `debug_logging_enabled`: stored/exposed in status but no log-level code reads
  it; its UI control is disabled.
- Automatic Ride Zero, ride detection, minimum ride duration, cadence timeout,
  last-ride storage, and last-ride MQTT publication: not implemented.
- BLE advertising power and automatic-advertising settings: stored only; the
  BLE service uses fixed radio/advertising behavior and the controls are
  disabled.
- Rotation-aware power: flag and sensor-independent types exist, but no runtime
  algorithm consumes them and the UI is disabled.
- Most IMU configuration fields (ODR/range/stationary timeout/confidence/cadence
  limits/direction/angle reference): stored only. The LSM6DS3 driver currently
  uses fixed 104 Hz, +/-4 g, and 500 dps active settings.
- `RuntimeMode`, `WakeClass`, and `RotationProvider` contracts: compiled but not
  used by `main.cpp` to select execution.
- Button wake and deep-sleep protection shutdown: not implemented.
- Factory reset and general reboot WebUI actions: not implemented.

## Configuration-field audit

This table covers the less obvious persisted fields. “Hidden” means firmware
uses the value but the current Settings page does not expose it.

| Fields | Status |
|---|---|
| `sample_interval_ms`, `publish_interval_ms` | Implemented, hidden. Control main sampling delay and BLE notification interval. |
| `inactivity_timeout_ms`, `light_sleep_enabled` | Implemented and editable. PowerManager receives changes only at boot, so restart is required. Cadence also uses inactivity timeout as its stale-RPM timeout. |
| `wake_on_timer_enabled`, `wake_on_usb_enabled` | Implemented internally and forced on by PowerManager. |
| `timer_wake_seconds`, `wake_on_imu_enabled`, `imu_wake_threshold`, `imu_wake_duration` | Implemented, hidden. Loaded at boot. |
| `deep_sleep_enabled`, `wake_on_button_enabled` | Unused. |
| `wifi_setup_on_usb`, `force_setup_portal` | Implemented internal setup gates. No dedicated user control; normal Settings save clears `force_setup_portal`. |
| `run_self_test_on_boot`, `self_test_done` | Implemented internal boot-test state. |
| `hx711_smoothing` | Implemented, hidden. Larger values retain more old data and therefore smooth more. |
| calibration/zero/scale/sign/mass/lever fields | Implemented and managed by Calibration actions. |
| `imu_revolution_threshold_dps` | Implemented by the temporary gyro-Z cadence provider, but disabled in UI because it is unsafe before installed validation. |
| BLE device name | Implemented and editable; applied on reboot. |
| MQTT enable/host/port/topic | Implemented and editable; plain TCP only. |
| MQTT percentage thresholds | Unused. |
| battery voltage scale/offset and four voltage thresholds | Implemented, hidden. |
| battery qualification count | Implemented, hidden. |
| battery check/heartbeat/report-delta/retry fields | Implemented, hidden, except the timer wake has its own separate interval. |
| battery hysteresis and USB report delta | Stored but unused. |
| maximum power and power-filter alpha | Implemented and editable; the main loop consumes saved values immediately. |
| Ride Diagnostics | Implemented and editable; serial logging only. |
| Automatic Ride Zero, ride detection, minimum ride duration, cadence timeout | Stored but unused. |
| rotation-aware power | Stored but unused. |
| debug logging | Stored but unused. |
| BLE advertising power/auto-advertise | Stored but unused. |
| IMU ODR/range/stationary/confidence/cadence-limit/direction/reference fields | Stored but unused; driver values are fixed. |
| legacy Wi-Fi flag and legacy tuning timeout | Migration/layout only; never product controls. |

## Removed or superseded designs

- Zigbee is not part of OpenWatts firmware.
- The separate timed IMU tuning session, arming flow, countdown, and timeout
  were removed. Maintenance Mode now authorizes continuous provisional IMU
  viewing. The old timeout field remains reserved only to preserve NVS layout.
- `Keep Wi-Fi on without USB` was replaced by Operating Mode. Its old stored bit
  remains only for schema migration.
- Deep sleep as the normal motion-wake state was rejected for this PCB because
  the fitted IMU interrupt is GPIO10, which cannot wake ESP32-C3 deep sleep.
- The restart-based ikoniWatts timer/report runtime was not activated in
  OpenWatts. Current code resumes from light sleep and uses a direct timer
  decision branch.

## Hardware validation still required

- Installed strain bridge response, noise, drift, saturation, return-to-zero,
  Bench Calibration, Manual Tare, and direction.
- Installed-crank IMU axis/sign, false-motion behavior, one-count-per-revolution,
  cadence accuracy, start/stop behavior, and motion wake sensitivity.
- BLE CPS cadence/power comparison against a trusted device during a ride.
- Battery divider scale/offset over charge and discharge, state thresholds,
  timer-report energy cost, and run time.
- Critical/protection behavior after an actual policy is implemented.
- OTA recovery behavior after interrupted/invalid uploads; current OTA has no
  signed-image or rollback policy beyond ESP-IDF image validation.

## Test coverage limits

The Python tests are host-side contract and source-structure tests. They check
math examples, required strings/routes, schema layout expectations, and the
presence of safety boundaries. They do not execute ESP-IDF drivers, radios,
sleep, NVS, HTTP, BLE, MQTT, IMU, HX711, or OTA on hardware.
