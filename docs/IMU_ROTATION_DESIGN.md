# IMU rotation design

`rotation.h` defines the sensor-independent contract for timestamped normalized
angle, angular velocity, cadence, direction, confidence, revolution identity
and boundary crossings. `CompletedRevolution` reserves the statistics needed by
a future revolution estimator: duration, coverage, sample count, average torque,
integrated torque-angle, raw/filtered power, confidence and rejection state.

These types are deliberately not fed by fabricated angle data. The current
LSM6DS3 cadence threshold is only a bring-up aid.

Future validated path:

`LSM6DS3 FIFO/interrupt → timestamped orientation/angle → revolution tracker`

`HX711 torque + rotation sample → angle-sector accumulator → completed revolution`

`validated work / duration → small output filter → BLE/MQTT/web`

Before enabling it:

1. Measure the installed crank axis and sign.
2. Characterize stationary bias and temperature drift.
3. Validate 60/90 RPM traces, acceleration, reversal and vibration.
4. Establish post-wake qualification and stale-data bounds.
5. Verify angular coverage and missed-sector detection.

No advanced BLE cycling metrics are exposed until physically valid.
