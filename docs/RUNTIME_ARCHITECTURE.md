# Runtime architecture

## User-facing policy

The persistent Operating Mode is either **Normal** or **Maintenance**.

- Normal permits Wi-Fi with USB or for a battery report and permits inactivity
  light sleep when USB is absent and BLE is disconnected.
- Maintenance keeps Wi-Fi/WebUI active on battery, permits calibration and
  provisional IMU diagnostics, and disables inactivity sleep.

Operating Mode applies immediately and survives reboot.

## Actual execution model

Current OpenWatts does **not** dispatch through four exclusive restart-based
runtimes. `app_main()` initializes board IO, battery ADC, HX711, IMU, Wi-Fi as
policy permits, and BLE, then runs one application loop.

Normal riding loop:

`sample HX711 + IMU -> cadence stub -> torque/power -> WebUI snapshot -> MQTT policy -> BLE notify -> sleep decision`

Normal inactivity sleep:

`stop BLE/Wi-Fi -> power down HX711 -> low-power IMU -> light sleep`

Motion or USB wake:

`resume IMU/HX711/BLE -> reset cadence and power acquisition -> main loop`

Timer wake:

`read battery -> qualify state -> decide report`

- No report: return directly to light sleep.
- Report: start Wi-Fi/MQTT in the existing process, finish the publish, then
  return to light sleep.

The timer branch avoids the normal sampling body for silent checks, but all
drivers were initialized before the original sleep. It is therefore not a
fresh minimal boot runtime.

## Compiled scaffolding

`runtime.h/.cpp` names Riding, USB Maintenance, Battery Check, and Battery
Report runtimes. `rotation.h/.cpp` defines future sensor-independent rotation
contracts. Neither currently controls `main.cpp`. They document intended
separation only and must not be cited as implemented runtime isolation.

The restart-based `TIMER_DECISION -> REPORT` architecture validated in
The restart-based timer/report architecture was considered but not activated. OpenWatts currently
uses resumed light sleep because its IMU wake path still requires installed
hardware validation.
BLE connection prevents inactivity sleep. A motion interrupt is not counted as
a revolution; cadence and power filters reset after wake and must reacquire.
