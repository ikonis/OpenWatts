# OpenWatts Bench Test

Before connecting the bridge, confirm USB, battery, Wi-Fi, OTA, BLE advertising,
IMU identity/vectors and honest strain-signal classification. HX711 conversions
with an open input are not proof of a complete bridge.

After connection, verify raw response, return-to-zero, noise and lack of
saturation before Bench Calibration. Automatic Ride Zero remains calibration-
gated and may be enabled after the unloaded bridge is stable.

Current Ride Diagnostics writes a compact serial line; it does not create a
high-rate MQTT trace. The Diagnostics-page provisional tracker is isolated from
the active cadence and power pipeline.
