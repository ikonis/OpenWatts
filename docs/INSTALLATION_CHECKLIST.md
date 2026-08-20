# First installation checklist

1. Charge the battery and compare WebUI voltage with a meter.
2. Select Maintenance Mode; confirm Wi-Fi survives USB removal.
3. Confirm LSM6DS3 identity/vectors and HX711 readiness.
4. Connect the bridge and verify counts respond to load and return near zero.
5. Perform Bench Calibration, save, verify known load, and confirm direction.
6. Mount the enclosure and secure every bridge/USB/battery connection.
7. Confirm Normal Mode inactivity sleep, IMU wake, and USB wake.
8. Return to Maintenance, remove USB, and capture installed-crank IMU behavior.
9. Compare active IMU cadence against counted revolutions and the previously
   validated installed-crank behavior.
10. Pair BLE through SmartSpin2K and perform a short controlled verification
    ride.
11. Return to Normal Mode when maintenance is finished.

The Diagnostics page contains a shorter checklist. Use `VALIDATION.md` for
acceptance criteria and `IMPLEMENTATION_STATUS.md` for the current product
boundary.
