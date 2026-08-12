# IMU cadence design

OpenWatts uses the crank-mounted LSM6DS3 gyroscope as its cadence source. The
installed RevA direction is negative gyro Z. The active estimator:

1. learns stationary gyro bias;
2. subtracts bias from each valid sample;
3. integrates forward angular motion;
4. counts each complete accumulated 360-degree rotation;
5. rejects reverse motion and implausible revolution intervals;
6. derives cadence and BLE crank event time from accepted revolutions;
7. returns cadence to zero after the configured stale timeout.

Crank position is relative; starting at a particular angle is never required.
A wake interrupt signals motion only and cannot itself create a revolution.

Power is integrated from non-negative calibrated torque across each completed
forward revolution, then passed through the output filter. Crank-angle sector
weighting is intentionally disabled because the product has no absolute angle
reference. Maintenance diagnostics expose enough gyro/integration evidence to
investigate field cadence problems without changing production output.
