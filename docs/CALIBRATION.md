# Calibration

Bench calibration and runtime zero are separate concepts.

Saved product calibration consists of unloaded raw offset, known-load scale,
lever arm/crank length, and torque direction. Runtime code must never overwrite
those values. Automatic runtime tare is intentionally disabled.

The current configuration preserves `zero_offset_counts`, `counts_per_nm`, and
`torque_sign` across the v3→v4 migration. A future product UI may add guided
unloaded/known-load capture, but it must preserve the same separation.
