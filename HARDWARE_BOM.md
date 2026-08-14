# OpenWatts RevA hardware BOM

This BOM covers the manufactured RevA board. It is based on the checked-in
schematic, PCB, and July 6 PCBWay assembly files. Where only a value or package
is known, that is all that is listed.

## MCU and sensors

| References | Qty | Component | Exact value / part | Package / footprint | Purpose |
|---|---:|---|---|---|---|
| U5 | 1 | Wi-Fi/BLE MCU module | ESP32-C3-MINI-1 | Espressif ESP32-C3-MINI-1 | Firmware, BLE CPS, Wi-Fi/WebUI, power policy |
| U6 | 1 | 6-axis IMU | LSM6DS3 | LGA-14, 3 x 2.5 mm, 0.5 mm pitch | Cadence, motion detection, wake interrupt |
| U4 | 1 | 24-bit bridge ADC | HX711 | SOIC-16, 3.9 x 9.9 mm, 1.27 mm pitch | Strain-bridge measurement |

## Power and charging

| References | Qty | Component | Exact value / part | Package / footprint | Purpose |
|---|---:|---|---|---|---|
| U1 | 1 | Single-cell LiPo charger | MCP73831-2-OT | SOT-23-5 | 4.20 V linear charging and charge-status output |
| U2 | 1 | 3.3 V LDO | ME6211C33M5 | SOT-23-5 | Regulated 3.3 V rail |
| R3 | 1 | Resistor | 2 kOhm | 0603 | Charger programming/support |
| C2 | 1 | Capacitor | 4.7 uF | 0603 | Power filtering |
| C3, C4 | 2 | Capacitor | 1 uF | 0603 | Power filtering |
| C6 | 1 | Capacitor | 10 uF | 0603 | Bulk rail filtering |

## Strain measurement and sensors

| References | Qty | Component | Exact value / part | Package / footprint | Purpose |
|---|---:|---|---|---|---|
| C1, C5, C8 | 3 | Capacitor | 100 nF | 0603 | Local decoupling |
| R4, R5 | 2 | Resistor | 4.7 kOhm | 0603 | IMU I2C pull-ups |
| J_TOP1, J_BOTTOM1 | 2 | 4-pin right-angle connector | JST XH S4B-XH-A family footprint | 1 x 4, 2.50 mm pitch | Top/bottom strain-pair connections |

## USB, programming, controls, and indication

| References | Qty | Component | Exact value / part | Package / footprint | Purpose |
|---|---:|---|---|---|---|
| J1 | 1 | USB-C USB 2.0 receptacle | HRO TYPE-C-31-M-12 footprint | 16-pin SMT/TH shield | USB power, native USB data, firmware service |
| R1, R2 | 2 | USB-C CC resistor | 5.1 kOhm | 0603 | Sink-role CC pull-downs |
| J2 | 1 | Service header | Generic 2 x 3 | 2.54 mm vertical | 3.3 V, GND, UART TX/RX, EN, BOOT |
| SW1, SW2 | 2 | Momentary push button | EVQP2-style middle-push footprint | SMD, H2.5 mm | EN/reset and GPIO9 boot controls |
| D2, D3 | 2 | Indicator LED | Color/order defined by assembled board | 0603 | Charge and MCU-controlled status indication |
| R6, R7 | 2 | LED resistor | 1 kOhm | 0603 | LED current limiting |
| R8-R11 | 4 | Resistor | 10 kOhm | 0603 | MCU boot/reset and signal biasing |

## Battery and USB sensing

| References | Qty | Component | Exact value / part | Package / footprint | Purpose |
|---|---:|---|---|---|---|
| R_BAT_TOP1, R_BAT_BOT1 | 2 | Resistor | 100 kOhm | 0603 | Battery-voltage ADC divider |
| R_USB_TOP1, R_USB_BOT1 | 2 | Resistor | 100 kOhm | 0603 | USB-present sensing divider |
| J_BAT1 | 1 | Battery connector | JST-PH B2B-PH-K family footprint | 1 x 2, 2.00 mm vertical | Single-cell LiPo connection |

## External build materials

These are required for the complete device but are not PCB assembly items.
Where the repository does not establish an exact purchasable part, none is
invented here.

| Item | Known requirement | Notes |
|---|---|---|
| Strain gauges | Bonded foil gauges wired as the installed crank bridge | Exact gauge manufacturer/part is not established in the repository |
| Battery | Protected single-cell LiPo compatible with the JST-PH connection and enclosure | Capacity and exact supplier are build-specific |
| Crank | Left crank prepared for bonded gauges and enclosure mounting | Calibration is installation-specific |
| Enclosure | See `mechanical/OpenWatts_RevA_Enclosure.step` and assembly model | Verify print/material/fastening for the actual installation |
| Wiring/connectors | Mating JST-PH battery and JST-XH strain harnesses | Observe actual polarity and connector orientation |
| Gauge adhesive/protection | Suitable strain-gauge bonding and environmental protection system | Exact product is not established in the repository |

## About the generated BOM

`OpenWatts.kicad_pcb` and `fab/pcbway-2026-07-06/` describe the manufactured
geometry. `production/bom.csv` is a generated engineering export; this document
corrects its misleading per-reference LED quantities by listing D2 and D3 once
each.
