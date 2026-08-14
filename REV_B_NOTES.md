# Exploratory RevB notes

This document preserves lessons learned from manufacturing, assembling, and
using **OpenWatts RevA**. RevA is the current working hardware. No RevB is in
development, no RevB is promised, and none of the ideas below modify or
supersede the reproducible RevA KiCad/fabrication files.

## 1. Confirmed issues with current hardware

- **BOOT/RESET silkscreen association:** the checked-in PCB places SW1 on
  `EN` (reset) beside the `BOOT` text and SW2 on GPIO9 (`BOOT`) beside the
  `RESET` text. A future layout should correct and re-check those labels against
  the schematic nets and physical button order.
- **No deep-sleep IMU wake:** RevA connects the LSM6DS3 interrupt to GPIO10,
  which is not an ESP32-C3 deep-sleep wake pin. The working firmware therefore
  uses light sleep. A future revision should select and validate a wake-capable
  pin if deep sleep is a real requirement.
- **Voltage-only state of charge:** RevA measures battery voltage and estimates
  percentage. Physical testing showed why voltage under charge/load is not a
  direct measurement of remaining capacity or charge time.
- **No charger power-path/load sharing:** the MCP73831 is a charger, not a
  complete power-path controller. USB operation, system load, and charging are
  not independently managed as they would be by a dedicated load-sharing
  design.
- **USB bring-up was sensitive:** the assembled board showed intermittent USB
  enumeration and host-controller disruption during early bring-up. Cable/panel
  hardware and power state were confounding factors, so the PCB-level cause was
  not proven. This is a confirmed validation concern, not a confirmed circuit
  defect.

## 2. Quality-of-life improvements

- Make service/test access usable after enclosure installation, especially EN,
  BOOT, UART, battery, USB-present, charge-status, I2C, HX711 data/clock, and
  key rails.
- Provide unambiguous polarity and pin-1 marking for battery and strain
  connectors, visible with the assembled enclosure/harness.
- Review whether reset/boot controls and the service header remain reachable in
  the actual crank installation.
- Preserve firmware control of the white/status LED, but label the independent
  charger LED and MCU LED by function rather than color alone.

## 3. Power/battery improvements

- Review a charger with proper power-path/load-sharing behavior so USB-powered
  operation, charging, and battery supply are explicitly managed.
- Review which charger-state signals firmware should receive: power-good,
  charging, charge-complete, fault, and battery-present where practical.
- Consider a real LiPo fuel gauge. **MAX17055 is only a candidate**, previously
  considered because a suitable implementation may provide current, state of
  charge, learned capacity, and runtime estimates. Part choice, sense topology,
  quiescent current, package, battery model, and firmware support all require a
  fresh design review.
- Re-evaluate battery ADC divider current, accuracy, calibration, and behavior
  during USB charging even if a fuel gauge is added as a fallback/diagnostic.

## 4. PCB/layout/silkscreen improvements

- Correct the confirmed BOOT/RESET label swap and audit every other label
  against its actual net and installed viewing orientation.
- Increase or reposition the small 0.75 mm silkscreen text that currently falls
  below the project's 0.80 mm DRC minimum.
- Resolve or intentionally suppress the overlapping D2/D3 footprint
  silkscreens; the manufactured placement should first be checked visually.
- Review connector orientation and cable exit direction using the final crank
  enclosure rather than the bare PCB alone.
- Review component and test-point placement for probe clearance, strain wiring,
  battery routing, enclosure walls, and assembly sequence.
- Keep polarity, strain-pair identity, and board orientation readable after
  installation—not only in an unobstructed PCB editor view.

## 5. Candidate component changes

- A fuel-gauge IC such as MAX17055 is a candidate, not a selection.
- A charger/power-path IC may replace or supplement the MCP73831 function, but
  no candidate is approved.
- USB protection/conditioning parts may be considered only after reproducing
  and measuring the earlier host-enumeration problem. Possible changes must be
  driven by evidence, not by assuming the PCB caused every observed USB event.

## 6. Ideas requiring further investigation

- Reproduce USB attach behavior with known-good direct cables, controlled
  battery states, current-limited power, and oscilloscope/USB analysis before
  proposing data-line or power changes.
- Measure actual RevA sleep, ride, Wi-Fi, and charge currents to establish the
  value and acceptable quiescent-current budget of a fuel gauge/power-path IC.
- Determine whether deep sleep materially improves real product runtime enough
  to justify an MCU-pin/layout change.
- Inspect assembled silkscreen, connector clearances, and probing access inside
  the final enclosure and annotate photographs before beginning any layout.
- Revisit ESD, reverse-current/back-power, inrush, and USB power-domain
  protection as a complete system review if a future board is ever authorized.

These are notes for a possible future board, not changes for RevA. Nothing here
belongs in the current schematic or PCB unless another revision is actually
started.
