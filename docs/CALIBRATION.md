# Calibration

Bench Calibration captures three-second multi-sample unloaded and loaded
windows. It rejects insufficient samples, excessive variance, excessive
peak-to-peak noise, tiny load deltas, non-finite math, and implausible scale.
Known torque is `mass_kg × 9.80665 × lever_arm_mm / 1000`.

Saving persists the permanent unloaded reference, counts per Nm, direction,
reference mass and lever arm. Runtime zero is separate. Manual Tare changes only
runtime zero; Reverse Direction changes only sign; Reset requires explicit
confirmation.

HX711 conversions cannot prove every bridge wire is intact. The interface uses
honest states: signal unavailable, saturated, unstable, present but not
calibrated, and calibrated. Physical load response must be verified first.

Bench calibration and runtime zero are separate concepts.

Saved product calibration consists of unloaded raw offset, known-load scale,
lever arm/crank length, and torque direction. Runtime code must never overwrite
those values. Automatic Ride Zero can update only the runtime zero, requires a
valid Bench Calibration and a stable 32-sample unloaded window, and locks once
a valid ride begins.

The current schema preserves legacy `zero_offset_counts`, `counts_per_nm`, and
`torque_sign` while using separate permanent and runtime zero fields. The guided
unloaded/known-load workflow is implemented in the Calibration page. Physical
validation on the installed OpenWatts bridge is still required.
