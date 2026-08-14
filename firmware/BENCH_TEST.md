# OpenWatts Bench Test

This is a service/reassembly procedure for the validated RevA product, not an
uncompleted bring-up checklist. Repeat it after bridge, enclosure, connector, or
crank work that could affect sensor integrity or calibration.

Before connecting the bridge, confirm USB, battery, Wi-Fi, OTA, BLE advertising,
IMU identity/vectors and honest strain-signal classification. HX711 conversions
with an open input are not proof of a complete bridge.

After connection, verify raw response, return-to-zero, noise and lack of
saturation before Bench Calibration. Automatic Ride Zero remains calibration-
gated and may be enabled after the unloaded bridge is stable.

Current Ride Diagnostics writes a compact serial line; it does not create a
high-rate MQTT trace. The Diagnostics-page provisional tracker is isolated from
the active cadence and power pipeline.
