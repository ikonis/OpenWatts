# Validation checklist

## USB bench

1. Confirm GPIO8 tracks USB present and GPIO5 charge status polarity.
2. Confirm AP `OpenWatts-Setup` and `http://192.168.4.1/`.
3. Verify existing credentials and calibration survived migration.
4. Verify HX711 missing/connected behavior and raw counts.
5. Confirm BLE CPS advertises and negative/invalid power reads zero.

## Stationary IMU

Record WHO_AM_I, all axes for at least 60 seconds, noise, bias and INT level.
Vibration without rotation must not increment revolutions.

## Hand rotation

Capture slow forward/reverse turns, identify the actual crank axis and sign, and
verify one boundary per revolution. Do not enable rotation-aware power yet.

## First short ride

Compare cadence with a trusted reference at 60 and 90 RPM. Check start/stop,
reconnect, stale-data reset, maximum-power rejection and BLE CPS compatibility.

## Ride diagnostics

Enable only for a bounded test. Capture timestamped IMU axes, raw/filtered HX711,
torque, cadence, raw/filtered power, rejection reason and sensor state. Disable
after capture because Wi-Fi/high-rate telemetry increases battery consumption.
