# OpenWatts project context

OpenWatts is a standalone ESP32-C3/LSM6DS3/HX711 cycling power meter. The RevA
PCB is assembled and fixed; firmware must follow `firmware/src/board.h` and must
not assume hardware changes.

Firmware 1.2.0 is the working-product baseline. Further work is feature work or
targeted corrective maintenance; a RevB is not currently planned.

## Product policy

- Normal Mode prioritizes riding and battery life.
- Maintenance Mode intentionally keeps Wi-Fi and service tools available.
- BLE Cycling Power is the riding interface.
- Bench Calibration establishes permanent scale and direction.
- Manual Tare and Automatic Ride Zero adjust only the in-service zero.
- IMU cadence and left-crank HX711 torque feed one authoritative power pipeline.
  The completed left-side work estimate is doubled once to produce conventional
  estimated total rider power for BLE, rides, MQTT, and road estimates. Raw
  strain and torque diagnostics remain left-side measurements.
- The most recent qualified ride is retained with reproducible road-model data.
- MQTT is low-rate reporting and Home Assistant integration, not live power
  transport.

## Runtime constraints

OpenWatts resumes one initialized application after light sleep. GPIO10 cannot
wake ESP32-C3 from deep sleep, so the LSM6DS3 motion interrupt uses light sleep.
Timer wakes take a direct battery-policy branch and return to sleep when no
report is required.

Normal Mode permits Wi-Fi for USB, the live dashboard during a ride candidate,
or a bounded report. A pending
Last Ride does not itself prevent the inactivity timer. Once sleep is otherwise
due, one QoS 1 publish is attempted for at most 15 seconds; success clears the
durable pending flag and failure defers retry without blocking sleep.

## Persistence contract

Wi-Fi credentials, Operating Mode, calibration, Ride Zero, report policy, and
Last Ride are product data and must survive OTA. `DeviceConfig` is an append-only
binary NVS schema. Reserved compatibility slots remain in the struct because
removing or reordering them would corrupt deployed settings. Last Ride has an
explicit versioned migration path.

## Known limits

- MQTT uses unauthenticated plain TCP.
- Battery percentage is estimated; voltage is authoritative.
- Grade, wind, coasting, and virtual inertia are outside road-model version 1.
- The diagnostic build currently shares product behavior and enables a compile
  marker for future controlled instrumentation.
