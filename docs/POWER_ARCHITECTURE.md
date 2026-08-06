# Battery and reporting policy

Operating Mode is the top-level power policy input. Normal Mode uses production
sleep and makes Wi-Fi available only with USB or during a policy-required report.
Maintenance Mode deliberately keeps Wi-Fi and the WebUI active on battery,
disables inactivity sleep, and permits calibration and tuning workflows.

Battery voltage is authoritative. Estimated percentage is informational.

Qualified states are Healthy, Charge Soon, Charge Now, Critical, Protection and
Invalid. State changes require two matching readings by default and use 50 mV
hysteresis at the product-policy boundary. Default thresholds are 3.65, 3.50,
3.35 and 3.20 V respectively.

Battery checks and network reports are independent:

- Check every 300 seconds.
- Report a qualified state change.
- Report a change of at least 0.02 V from the last successfully reported
  voltage.
- Retry a failed report after 15 minutes.
- Heartbeat after 24 hours since the last successful report.
- While USB-powered, voltage reporting may use a 0.01 V delta.

Maintenance Mode evaluates MQTT every 5 seconds on USB and every 30 seconds on
battery. These fixed service cadences are intentionally not user settings.
Normal Mode continues to use the battery report decision policy above.

Critical and Protection states suppress battery-powered Wi-Fi. The check path
must return to sleep without BLE, web, MQTT or riding initialization when no
report is needed.

The actual divider scale and offset remain hardware-validation items. Firmware
must not infer them from the schematic alone.
