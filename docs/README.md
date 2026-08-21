# OpenWatts documentation

The documents below describe the assembled OpenWatts RevA and firmware 1.2.0.
They are not a substitute for checking the actual board, harnesses, and crank
installation before applying power or load.

## Use and setup

- [Installation checklist](INSTALLATION_CHECKLIST.md)
- [Calibration](CALIBRATION.md)
- [Web interface](WEB_UI.md)
- [SmartSpin2K Trainer Test](TRAINER_TEST.md)
- [Home Assistant](HOME_ASSISTANT.md)

## Architecture and validation

- [Current product status](IMPLEMENTATION_STATUS.md)
- [Power, sleep, and reporting](POWER_ARCHITECTURE.md)
- [Runtime architecture](RUNTIME_ARCHITECTURE.md)
- [IMU cadence and tuning](IMU_ROTATION_DESIGN.md)
- [IMU tuning workflow](IMU_TUNING.md)
- [Validation checklist](VALIDATION.md)
- [Project context](PROJECT_CONTEXT.md)

## Hardware exports

The [hardware folder](hardware/) contains browser-friendly exports of the
manufactured RevA schematic and board. The editable sources remain in the
repository root as `OpenWatts.kicad_sch` and `OpenWatts.kicad_pcb`.
