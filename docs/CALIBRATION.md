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
those values. Automatic runtime tare is intentionally disabled.

The current configuration preserves `zero_offset_counts`, `counts_per_nm`, and
`torque_sign` across the v3→v4 migration. A future product UI may add guided
unloaded/known-load capture, but it must preserve the same separation.
