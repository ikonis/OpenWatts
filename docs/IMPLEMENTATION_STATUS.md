# OpenWatts implementation status

This is the production-source audit for firmware 1.1.0. The two PlatformIO
targets are `esp32c3` and `esp32c3_diagnostic`; persisted `DeviceConfig` schema
is 13 and Last Ride schema is 3.

## Implemented

| Area | Current behavior |
|---|---|
| Operating Mode | Normal and Maintenance apply immediately and persist. |
| BLE CPS | Configurable identity/radio power, signed-safe power, crank revolution and event-time data, delivery/failure counters. |
| Cadence | Stationary-bias-corrected forward gyro-Z integration, complete-revolution counting, reverse rejection, cadence range and stale-time validation. |
| Torque/power | HX711 signed conversion, readiness/failures/noise, Bench Calibration, Manual Tare, Automatic Ride Zero, sample validation, revolution work integration, median/EMA output filtering. |
| Ride history | Calibration-gated detection, one retained Last Ride, power/cadence/work/revolutions, versioned road distance/speed context, durable post-ride MQTT intent. |
| Sleep/wake | Normal inactivity light sleep with IMU, USB and timer wake; direct timer battery decision; Maintenance intentionally remains awake. |
| Reporting | Voltage-first battery policy, retained QoS 1 MQTT, Home Assistant discovery, bounded pre-sleep Last Ride report and retry persistence. |
| Wi-Fi/WebUI | Station mode, recovery AP/wildcard DNS, responsive Status/Ride/Settings/Calibration/OTA/Diagnostics pages and SmartSpin2K live dashboard. |
| OTA | Image/size/partition validation and reboot; USB or qualified Maintenance battery power. |
| Configuration | One append-only NVS config with centralized sanitization and explicit Last Ride migration. |
| LED | Product boot, advertising, connected, maintenance, OTA, calibration-required and fatal patterns. |

## Intentionally limited

- GPIO10 requires light sleep for IMU motion wake; deep sleep is not available
  for normal riding on RevA.
- Road model version 1 assumes flat ground, still air and no coasting/inertia.
- MQTT is unauthenticated plain TCP; TLS and broker credentials are absent.
- Battery percentage is an estimate; voltage is authoritative.
- Calibration quality and power accuracy depend on the installed bridge and
  crank mechanics, not firmware alone.
- SmartSpin2K dashboard fields are browser-side, read-only LAN data and do not
  alter the working BLE topology.
- The diagnostic target currently preserves product behavior and provides a
  compile marker for controlled future instrumentation.

## NVS compatibility retained intentionally

`DeviceConfig` is an append-only binary blob. Historical deep-sleep, button,
Wi-Fi, BLE-advertising and IMU-session slots remain reserved at their deployed
offsets. They are not controls and are not executed. Removing or reordering
them would risk existing Wi-Fi credentials and calibration.

Last Ride schemas 1 and 2 migrate explicitly. Schema 3 adds durable MQTT
pending state. A schema-2 ride migrates as already published to avoid an
invented duplicate report after OTA.

## Debug behavior

Normal operation retains compact failure/readiness counters needed for field
support. Debug Logging enables verbose logs; Ride diagnostics enables periodic
engineering ride lines. Both remain off by default. Diagnostics-page HTTP JSON
is formatted only when requested.

## Validation boundary

Host tests cover policy/math/contracts and builds cover ESP-IDF integration.
Every release still needs physical OTA, reboot/persistence, WebUI, BLE,
IMU/HX711, sleep/wake and MQTT validation on the assembled device.
