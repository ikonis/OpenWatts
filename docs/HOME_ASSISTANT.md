# Home Assistant

OpenWatts publishes retained MQTT telemetry to the configured topic and uses
Home Assistant MQTT discovery. The device identifier is `openwatts`; this is
separate from ikoniWatts and does not replace its dashboard or entities.

## Dashboard

The storage dashboard is named **OpenWatts** and is available at
`/openwatts-dashboard/status`. It contains Battery, Device, and Last Ride
sections.

## Last Ride

A ride is recorded only when Bench Calibration is valid, ride detection is
enabled, cadence remains present for the candidate period, and the configured
minimum ride duration is reached. Only the most recent completed ride is kept.
Its retained MQTT fields are:

- moving and elapsed time
- average and peak power
- average and peak cadence
- crank revolutions
- mechanical work
- ride end reason

The Last Ride entities remain unavailable until a qualified calibrated ride
has completed. Completion requests one retained MQTT report; routine live
samples are not sent to Home Assistant.

## Network requirement

The OpenWatts address must be permitted to reach the MQTT broker on its
configured TCP port. With the current addresses this is
`192.168.20.148 -> 192.168.1.28:1883`. MQTT discovery and dashboard values will
remain absent if that inter-VLAN path is blocked.
