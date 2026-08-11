# IMU Tuning Plan

Rotation-aware power remains unavailable until first-ride validation. The
installed RevA cadence path uses bias-corrected negative gyro-Z integration,
counts each accumulated 360-degree forward rotation, and rejects positive-Z
reverse motion. Provisional Diagnostics-page tuning data remains separate
and never feeds BLE, power estimation, calibration, or Ride Zero.

Select **Maintenance** in Settings. Live IMU tuning then remains available on
USB or battery for as long as Maintenance Mode is selected. There is no tuning
session, arming step, countdown, or timeout. Select **Normal** when work is
finished to restore production Wi-Fi and sleep policy. Battery consumption is
intentionally high in Maintenance Mode.

It exposes sensor vectors, interrupt/motion state, provisional dominant-axis
angle and velocity, tentative cadence/revolution output, confidence and a
reason. The tracker can be reset, but it does not save tuning values or modify
the cadence provider. Physical testing must establish
axis, direction, stationary thresholds, cadence limits, wrap detection,
staleness and confidence before normal controls are enabled.

Crank position is never a user calibration requirement. Capture and future
production cadence detection must start from any physical crank angle. The
first valid orientation establishes a relative phase; revolutions are counted
from accumulated rotation, not from a remembered top/bottom position.

The Diagnostics page provides a temporary RAM-only capture at up to 20 Hz for
1,200 samples. It records timestamped accelerometer and gyroscope vectors and exports
CSV. Captures are volatile, do not modify NVS, and do not feed BLE, power,
calibration, or Ride Zero.
