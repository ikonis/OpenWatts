# OpenWatts

OpenWatts is a crank-mounted, single-sided cycling power meter built around an
ESP32-C3. A bonded strain-gauge bridge and HX711 measure crank torque, while an
LSM6DS3 IMU provides cadence and motion wake. Firmware publishes standard BLE
Cycling Power Service data and includes calibration, ride recording, battery
management, OTA updates, a responsive WebUI, and optional MQTT/Home Assistant
reporting.

![OpenWatts RevA PCB](docs/hardware/OpenWatts-RevA-3d.png)

The current hardware is the manufactured and field-validated **OpenWatts
RevA**. It has been calibrated and ridden through the working topology:

`OpenWatts --BLE CPS--> SmartSpin2K --> MyWhoosh`

## Current capabilities

- calibrated strain/HX711 torque and IMU cadence
- signed-safe, filtered BLE Cycling Power output
- Normal and Maintenance operating modes
- motion wake and battery-conscious light sleep
- guided Bench Calibration plus runtime Ride Zero
- phone-first live ride dashboard with read-only SmartSpin2K LAN data
- retained Last Ride statistics and modeled road distance/speed
- USB or guarded Maintenance-mode web OTA
- retained MQTT battery/ride reports and Home Assistant discovery

## Hardware

- [KiCad schematic](OpenWatts.kicad_sch)
- [KiCad PCB](OpenWatts.kicad_pcb)
- [human-readable hardware BOM](HARDWARE_BOM.md)
- [public hardware exports](docs/hardware/)
- [manufactured PCBWay RevA package](fab/pcbway-2026-07-06/)
- [installation checklist](docs/INSTALLATION_CHECKLIST.md)
- [calibration guide](docs/CALIBRATION.md)

The checked-in KiCad PCB describes the board that was actually manufactured;
it is not a hypothetical redesign. Exploratory future lessons are deliberately
isolated in [REV_B_NOTES.md](REV_B_NOTES.md) and are not a development promise.

## Firmware

Firmware source, build instructions, and its hardware boundary are documented
in [firmware/README.md](firmware/README.md). Product behavior and validation are
covered by the focused documents in [`docs/`](docs/).

## Important limitations

- RevA uses light sleep because its IMU interrupt is not connected to an
  ESP32-C3 deep-sleep-capable GPIO.
- Battery percentage is estimated from voltage; voltage is authoritative.
- MQTT uses unauthenticated plain TCP on a trusted LAN.
- Road speed and distance are modeled estimates, not wheel/GPS measurements.

## License

No license file is currently included. Until one is added, normal copyright
rules apply; public visibility alone does not grant reuse rights.
