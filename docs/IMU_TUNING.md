# IMU Tuning Plan

Production-quality cadence and rotation-aware power remain unavailable until
installed-crank validation. A threshold-crossing bring-up cadence stub is active
and does feed current BLE/power calculations; it must not be mistaken for a
validated algorithm. Provisional Diagnostics-page tuning data remains separate
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
