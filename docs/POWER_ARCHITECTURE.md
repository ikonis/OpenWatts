# Battery, sleep, and reporting behavior

Operating Mode is the top-level user policy.

- Normal: Wi-Fi with USB or a required report; inactivity light sleep allowed.
- Maintenance: Wi-Fi/WebUI remain active on battery; inactivity sleep disabled;
  MQTT evaluation occurs every 5 seconds on USB and 30 seconds on battery.

## Measurement and display

GPIO4 is sampled through the calibrated ADC driver. Each reading is a trimmed
mean of 15 conversions through the configured scale/offset. Voltage is valid
from 3.0 to 4.30 V and is authoritative.

Estimated percentage is display/report metadata. It uses a fixed curve from
3.20 V (0%) to the observed 4.16 V full endpoint, then an EMA and 6 mV display
hysteresis. Percentage does not drive battery state or report decisions.

## Qualified battery state

The active voltage thresholds are:

- Healthy: above 3.65 V
- Charge Soon: at or below 3.65 V
- Charge Now: at or below 3.50 V
- Critical: at or below 3.35 V
- Protection: at or below 3.20 V
- Invalid: outside the accepted measurement/classification range

A transition needs two matching observations by default. Qualification is
count-based rather than hysteresis-based.

## Report decisions

Normal report policy publishes after:

- first valid report after boot;
- qualified battery-state change;
- voltage change of at least 0.02 V from the last successful report;
- retry delay after a failed report (default 15 minutes);
- heartbeat age since the last successful report (default 24 hours);
- USB insertion/removal observed while awake.

Report history is held in RAM and resets on reboot. The retained MQTT message
survives at the broker, but firmware scheduling history does not. Both USB and
battery use the 0.02 V report delta unless Maintenance Mode applies its fixed
service cadence. State and report logic are voltage-based; estimated percentage
is display metadata only.

## Sleep and wake

Normal Mode enters light sleep only when USB is absent, BLE is disconnected,
and the configured inactivity delay has elapsed. Sleep stops BLE/Wi-Fi, powers
down HX711, and leaves the LSM6DS3 accelerometer in low-power wake mode.

Wake sources are IMU GPIO10, USB-present GPIO8, and a timer (default 300
seconds). The ESP32-C3 cannot deep-sleep wake from GPIO10, so normal motion wake
uses light sleep. `deep_sleep_enabled` exists in NVS but is not implemented.

The timer wake interval is `timer_wake_seconds`; MQTT evaluation in Normal Mode
uses `battery_check_interval_seconds`. Both default to 300 seconds but are
separate fields and are not user-editable in the current WebUI.

Critical and Protection states currently change labels and trigger normal state
reports. They do **not** yet force shutdown, suppress Wi-Fi, or invoke a special
protection path.
