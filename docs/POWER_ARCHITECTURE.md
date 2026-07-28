# Battery and reporting policy

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

Critical and Protection states suppress battery-powered Wi-Fi. The check path
must return to sleep without BLE, web, MQTT or riding initialization when no
report is needed.

The actual divider scale and offset remain hardware-validation items. Firmware
must not infer them from the schematic alone.
