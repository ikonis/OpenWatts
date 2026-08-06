# IMU Tuning Plan

Production cadence and rotation-aware power remain disabled until installed-
crank validation. Provisional tuning data never feeds BLE, power estimation,
calibration, or Ride Zero.

Select **Maintenance** in Settings. Live IMU tuning then remains available on
USB or battery for as long as Maintenance Mode is selected. There is no tuning
session, arming step, countdown, or timeout. Select **Normal** when work is
finished to restore production Wi-Fi and sleep policy. Battery consumption is
intentionally high in Maintenance Mode.

It exposes sensor vectors, interrupt/motion state, provisional dominant-axis
angle and velocity, tentative cadence/revolution output, confidence and a
rejection reason. The tracker can be reset. Physical testing must establish
axis, direction, stationary thresholds, cadence limits, wrap detection,
staleness and confidence before normal controls are enabled.
