# Runtime architecture

The user selects one persistent **Operating Mode**: Normal or Maintenance.
Normal applies production riding, Wi-Fi, MQTT, and sleep policy. Maintenance
keeps Wi-Fi and the WebUI available on battery and permits calibration, Manual
Tare, direction reversal, IMU tuning, raw data, and diagnostics. There is no
separate tuning session, arming step, or timeout.

Operating Mode is a policy input, not an execution runtime. The internal
exclusive runtimes below remain implementation details. Timer and report
runtimes never initialize calibration, tuning, or riding algorithms.

OpenWatts uses the same conceptual exclusive runtimes as ikoniWatts:

| Runtime | Initializes | Must not initialize |
|---|---|---|
| NORMAL / Riding | IMU, HX711, cadence, BLE CPS | MQTT reporting unless Ride diagnostics is explicitly enabled |
| USB Maintenance | Wi-Fi/web/OTA and services permitted by Operating Mode | battery-only sleep path |
| TIMER_DECISION / Battery Check | battery ADC, retained policy state | BLE, IMU streaming, HX711 streaming, web |
| REPORT / Battery Report | battery ADC, Wi-Fi, MQTT | BLE, cadence, riding timers |

Validated target timer flow:

`timer wake → clean start → TIMER_DECISION → silent sleep`

or:

`timer wake → TIMER_DECISION → retain report intent → clean start → REPORT → sleep`

OpenWatts currently retains its pre-parity light-sleep resume loop because the
real LSM6DS3 interrupt polarity, post-wake sampling and battery ADC scale have
not yet been measured on hardware. The runtime enums and policy boundary are
present, but activating restart-based transitions before those measurements
would create an unverified sleep product. This is an explicit deferred item.

BLE connection must prevent inactivity sleep. Once disconnected, the configured
inactivity timer may begin. A motion wake is not a revolution; cadence/power
remain invalid until sufficient post-wake samples are observed.
