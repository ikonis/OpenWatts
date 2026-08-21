# SmartSpin2K Trainer Test

`/trainer-test` is a phone-first, temporary tuning tool for characterizing a
SmartSpin2K from OpenWatts power and cadence. It preserves the normal radio
topology:

`OpenWatts -- BLE CPS --> SmartSpin2K --> MyWhoosh`

The test page adds only browser-to-trainer LAN requests. It never changes
OpenWatts BLE CPS, calibration, power calculation, sleep policy, or normal MQTT
reporting.

## Before starting

- OpenWatts must be calibrated and either USB powered or in Maintenance Mode.
- `smartspin2k.local` must be reachable from the phone/browser.
- Stop MyWhoosh, QZ, and any other trainer-control application first.
- Keep pedaling safely. The automated tests stop when cadence stops, OpenWatts
  becomes invalid, SmartSpin2K disappears, or ERG control is lost.

Starting a test takes a volatile ten-minute lease that is renewed while the
page is open. During that lease OpenWatts does not record or replace Last Ride.
The lease is cleared by Abort, normal completion, reboot, or expiry; it is never
stored in NVS.

## Tests

### Minimum Brake Watts

This is manual: the page does not move the trainer. Set the trainer to its
minimum usable physical resistance, ride the requested 60/70/80/90/100 RPM
stages, and press **Next Stage** after each collection. The result highlights
the measured power nearest 90 RPM for comparison with SmartSpin2K's configured
`minWatts` value.

### ERG Step Response

Default targets are 150, 200, 250, 200, and 150 W. The browser asks the
SmartSpin2K WebUI for ERG control and each target after explicit confirmation.
It records response/settling estimates, steady error, variation, resistance,
and logical stepper position. The recommendation is advisory only; OpenWatts
does not modify `ERGSensitivity`.

### ERG Floor / Control Authority

Default requests descend from 180 to 80 W. This identifies a likely physical
floor when measured output stays materially above the request as resistance no
longer reduces. It is an observation, not an automatic SmartSpin2K setting
change.

### ERG Stability

Holds a configurable target (200 W for 120 seconds by default) and records
mean absolute target error and output variation.

## Control and data interface

The page reads SmartSpin2K's public WebUI endpoints once per second:

- `/runtimeConfigJSON`: current mode, requested/measured watts, cadence,
  resistance, target resistance, heart rate, homing state, and logical position.
- `/configJSON`: firmware version, `ERGSensitivity`, and `minWatts` context.

Automated tests additionally use the same documented WebUI endpoints:

- `/ergmode?value=enable` / `disable`
- `/targetwattsslider?value=<watts>` / `disable`

These are the handlers used by the current SmartSpin2K WebUI itself; see
[HTTP_Server_Basic.cpp](https://github.com/doudar/SmartSpin2k/blob/develop/src/HTTP_Server_Basic.cpp).
The page does not use undocumented BLE control commands, shift endpoints, or
direct stepper commands. **Abort** and normal completion request target disable
and ERG release before returning control.

## Results

The latest completed page result is retained in browser local storage, not in
OpenWatts NVS. Download JSON for the full samples or CSV for the stage summary.
This keeps test artifacts off the power meter and means clearing browser data
also clears the retained result.
