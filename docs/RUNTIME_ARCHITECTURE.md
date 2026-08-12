# Runtime architecture

## Operating policy

The persistent Operating Mode is **Normal** or **Maintenance**.

- Normal permits Wi-Fi for USB or bounded reporting and permits inactivity
  light sleep when USB is absent, BLE is disconnected, and a ride no longer
  needs finalization.
- Maintenance keeps Wi-Fi/WebUI and service tools active and disables
  inactivity sleep.

## Main execution

`app_main()` initializes board IO, battery ADC, HX711, LSM6DS3, policy-allowed
Wi-Fi, and BLE, then runs one application loop:

`sample -> cadence -> torque/power -> ride lifecycle -> status -> reporting -> BLE -> sleep decision`

There is one authoritative cadence (`CadenceEstimator`) and one authoritative
power sample (`PowerEstimator`) shared by BLE, ride logging, and the WebUI.

## Sleep and wake

Normal inactivity sleep:

`BLE disconnected + ride finalized + idle timeout -> optional bounded report -> Ride Zero -> light sleep`

Before light sleep, BLE/Wi-Fi are stopped, HX711 is powered down, and the IMU is
armed for motion. Motion or USB resumes the drivers and resets cadence/power
acquisition. A motion interrupt is never counted as a revolution by itself.

Timer wake:

`read battery -> qualify state -> evaluate report policy`

- No report: return directly to light sleep.
- Report: start bounded Wi-Fi/MQTT, finish or fail, then return to light sleep.

## Post-ride reporting

After five stationary minutes, a qualified ride is finalized and persisted with
`mqtt_publish_pending=true`. This does not start networking and does not block
the configured inactivity timeout. When normal sleep becomes due, firmware:

1. starts report-authorized Wi-Fi;
2. starts a retained QoS 1 MQTT publish;
3. waits up to 15 seconds for acknowledgement;
4. clears and persists the pending flag only after success;
5. stops networking and sleeps on either success or failure.

USB or another permitted report can also deliver a pending ride. NVS preserves
the record across resets and failed network attempts.
