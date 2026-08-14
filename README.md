# OpenWatts

OpenWatts is a crank-mounted, single-sided cycling power meter built around an
ESP32-C3. A bonded strain-gauge bridge and HX711 measure crank torque, while an
LSM6DS3 IMU provides cadence and motion wake. Firmware publishes standard BLE
Cycling Power Service data and includes calibration, ride recording, battery
management, OTA updates, a responsive WebUI, and optional MQTT/Home Assistant
reporting.

![OpenWatts RevA PCB](docs/hardware/OpenWatts-RevA-3d.png)

The board in this repository is **OpenWatts RevA**, the same revision currently
installed on my bike. It has been calibrated and used with:

`OpenWatts --BLE CPS--> SmartSpin2K --> MyWhoosh`

## Features

- calibrated strain/HX711 torque and IMU cadence
- filtered BLE Cycling Power output
- Normal and Maintenance operating modes
- motion wake and battery-conscious light sleep
- guided Bench Calibration plus runtime Ride Zero
- phone-first live ride dashboard with read-only SmartSpin2K LAN data
- Last Ride statistics and estimated road distance/speed
- web OTA over USB or while working in Maintenance mode
- MQTT battery/ride reports and Home Assistant discovery

## Hardware

- [KiCad schematic](OpenWatts.kicad_sch)
- [KiCad PCB](OpenWatts.kicad_pcb)
- [BOM](HARDWARE_BOM.md)
- [schematic and board images](docs/hardware/)
- [PCBWay manufacturing files](fab/pcbway-2026-07-06/)
- [installation checklist](docs/INSTALLATION_CHECKLIST.md)
- [calibration guide](docs/CALIBRATION.md)

The checked-in KiCad PCB is the board that was manufactured. Notes from building
and using it are kept separately in [REV_B_NOTES.md](REV_B_NOTES.md) in case I
ever make another revision.

## Firmware

Firmware source and build instructions are in
[firmware/README.md](firmware/README.md). The [`docs/`](docs/) folder has the
setup, calibration, architecture, Home Assistant, and test notes.

## Notes

- RevA uses light sleep because its IMU interrupt is not connected to an
  ESP32-C3 deep-sleep-capable GPIO.
- Battery percentage is estimated from voltage, so the voltage reading is the
  useful number when accuracy matters.
- MQTT uses unauthenticated plain TCP on a trusted LAN.
- Road speed and distance are modeled estimates, not wheel/GPS measurements.

## License

This repository does not have a license yet.
