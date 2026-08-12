# OpenWatts RevA requirements

This document describes the fabricated RevA hardware. It does not authorize PCB
changes.

## Functional

- Measure left-crank strain through HX711.
- Advertise Bluetooth Cycling Power Service.
- Use the fitted LSM6DS3 for motion wake and, after validation, cadence and
  rotational information.
- In Normal Mode, provide Wi-Fi/configuration only with USB; in deliberate
  Maintenance Mode, allow Wi-Fi/WebUI to remain active on battery. OTA remains
  USB-only.
- Provide optional, policy-controlled MQTT/Home Assistant battery reports.
- Report battery voltage and charging/USB state.
- Preserve calibration and credentials across firmware migration.

## Electrical

- ESP32-C3-MINI-1-H4X, 3.3 V system.
- Native USB on GPIO18/19.
- HX711 DOUT/SCK on GPIO0/1.
- LSM6DS3 over I2C GPIO6/7, INT on GPIO10.
- USB present GPIO8, charge status GPIO5, battery ADC GPIO4.
- GPIO10 is not RTC-capable on ESP32-C3; IMU wake therefore uses light sleep.

The firmware targets only the assembled OpenWatts RevA hardware described
above. PCB redesign is outside this repository's firmware scope.
