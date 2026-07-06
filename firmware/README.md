# OpenWatts Firmware Bring-Up

Initial ESP32-C3 firmware scaffold for the OpenWatts PCB.

## Hardware Assumptions

- Module: ESP32-C3-MINI-1-H4X.
- Normal runtime interface: BLE Cycling Power Service.
- Wi-Fi is for setup/configuration and USB-powered maintenance only.
- Wi-Fi remains off during normal battery runtime.
- Setup AP starts only when credentials are missing, or when USB is present and setup is requested with BOOT held.
- LSM6DS3 `SDO/SA0` is strapped low, so the IMU I2C address is `0x6A`.
- HX711 is connected directly to ESP32-C3 GPIOs.
- No Zigbee or Thread support is included.

## Pin Map

| Function | GPIO |
| --- | --- |
| UART_TX | GPIO21 |
| UART_RX | GPIO20 |
| USB_D+ | GPIO19 |
| USB_D- | GPIO18 |
| USB_PRESENT | GPIO8 |
| BOOT | GPIO9 |
| HX_DOUT | GPIO0 |
| HX_SCK | GPIO1 |
| I2C_SDA | GPIO6 |
| I2C_SCL | GPIO7 |
| IMU_INT | GPIO10 |
| GREEN_LED | GPIO3 |
| BAT_ADC | GPIO4 |
| CHG_STAT | GPIO5 |

## Current Scaffold

- PlatformIO + ESP-IDF target for ESP32-C3.
- Board pin map with compile-time GPIO assertions.
- Modern ESP-IDF `driver/i2c_master.h` I2C init.
- HX711 read/filter driver adapted from ikoniWatts.
- LSM6DS3 WHO_AM_I/config/read support.
- IMU-based cadence estimator stub.
- Torque/power estimator using placeholder calibration constants.
- BLE Cycling Power Service with crank revolution fields.
- BLE readable diagnostics characteristic `0xFFF1`.
- NVS-backed settings storage.
- Captive setup portal: `OpenWatts-Setup`, wildcard DNS redirect, HTTP setup page, `/save`, `/selftest`.
- First-boot or BOOT-requested hardware self-test.
- Configurable deep sleep after zero cadence, 60 seconds by default.

## Stubbed / TODO

- IMU cadence algorithm calibration, axis selection, filtering, and false-positive rejection.
- LSM6DS3 motion interrupt configuration for wake from deep sleep.
- ESP GPIO wake from USB/BOOT after wake-source electrical behavior is validated.
- HX711 zeroing, temperature drift handling, and torque calibration.
- Battery ADC divider scaling and eFuse ADC calibration.
- Production BLE Cycling Power feature bits after calibration fields are finalized.

## First Bring-Up Steps

1. Flash with USB and confirm serial boot logs.
2. Verify `USB_PRESENT` and `CHG_STAT` levels in logs.
3. Confirm LSM6DS3 `WHO_AM_I=0x69`.
4. Confirm HX711 raw counts change under load.
5. Confirm BLE advertises as `OpenWatts`.
6. On first boot, read serial self-test output and BLE diagnostics characteristic.
7. If credentials are missing, connect to `OpenWatts-Setup` and open `http://192.168.4.1/`.
8. Spin/rotate the crank fixture and log IMU axis data before replacing the cadence stub.
9. Disable or tune deep sleep during bench debugging if it interrupts testing.
