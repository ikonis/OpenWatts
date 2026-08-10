# Validation status and checklist

The repository contains host-side contract tests and successful build
configuration, but it does not contain a complete recorded hardware validation
report. Do not infer a physical pass from source code or a Python test.

## Automated coverage

`test_parity_architecture.py` and `test_installation_calibration.py` verify
selected math contracts, source boundaries, routes, NVS layout expectations,
disabled features, and UI wiring. They do not run ESP-IDF or exercise hardware.

The product and diagnostic PlatformIO targets compile the same runtime. The
diagnostic macro is currently unused.

## Electronics and maintenance validation

1. Confirm GPIO8 follows USB and GPIO5 active-low charge status is truthful.
2. With no saved credentials or setup requested, confirm `OpenWatts-Setup`,
   wildcard DNS, and `http://192.168.4.1/`.
3. With credentials, confirm station-only connection and reconnect behavior.
4. Switch Normal/Maintenance in both directions and confirm immediate NVS
   persistence, header badge, Wi-Fi policy, and sleep policy.
5. Confirm valid OTA succeeds only with USB, rejects invalid/oversize images,
   reboots, and preserves NVS.
6. Confirm missing/connected HX711 behavior, raw counts, read failures, and no
   crash when data is unavailable.

## Battery and sleep validation

1. Calibrate voltage scale/offset against a meter across charge/discharge.
2. Verify the 4.16 V display endpoint and estimated curve on actual cells.
3. Confirm qualification requires two observations and document transitions.
4. Confirm Normal inactivity sleep only after USB absence, BLE disconnect, and
   idle timeout.
5. Confirm IMU, USB, and timer wake independently.
6. Confirm silent timer decisions do not execute the normal sampling body.
7. Measure Maintenance battery cost at its 30-second MQTT cadence.
8. Do not claim hysteresis, USB-specific report delta, or protection shutdown;
   those paths are not implemented.

## Strain and calibration validation

1. Verify the connected bridge changes counts under load and returns near zero.
2. Record unloaded noise, drift, saturation margin, and temperature behavior.
3. Run Bench Calibration, save, verify the known load, and confirm direction.
4. Confirm Manual Tare changes runtime zero only.
5. Confirm direction reversal preserves zero and scale magnitude.
6. Confirm reset clears calibration only after explicit confirmation.

## IMU and first ride validation

1. Record stationary axes/bias and vibration response.
2. Identify the installed crank rotation axis and sign.
3. Compare threshold crossings against actual revolutions at slow, 60, and
   90 RPM, including reverse motion and start/stop.
4. Verify motion wake does not itself count a revolution.
5. Treat current BLE cadence/power as experimental until the cadence provider
   passes the above comparisons.
6. Verify BLE CPS pairing, signed-safe power, crank event timing, reconnection,
   and sleep after disconnect.

## Ride diagnostics limitation

The current Ride Diagnostics setting adds one periodic serial log line containing
RPM, power, raw HX711, sensor readiness, revolutions, BLE, and Wi-Fi state. It
does not publish high-rate MQTT traces and does not include the complete list of
raw/filtered/rejection metrics previously proposed.
