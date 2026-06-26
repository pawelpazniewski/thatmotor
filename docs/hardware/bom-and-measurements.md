# BOM and Hardware Measurement Plan

Branch: `feature/kayak-motor-firmware-v1`
Last updated: 2026-06-16

This document captures the hardware bill of materials and the measurement plan
that unblocks real firmware values (neutral, frame period, boot dead-window).
Firmware safety layers (boot-to-DISARMED, watchdog, hard clamp) sit on top of
the hardware guarantees listed here.

## Critical hardware guarantee (not firmware)

ESP32 outputs are Hi-Z (no internal pull) during the boot/reset/brownout dead
window, and on a brownout sag the core may emit a garbage pulse before reset.
The real safety guarantee in that window is hardware:

- **Pull-down resistors ~10 kΩ on GPIO18 (servo) and GPIO8 (ESC)** so the
  output lines cannot float during reset/boot/brownout.
- **ESC's own failsafe** on loss of signal (WP880 must drive to neutral when
  PWM disappears — to be verified by measurement).

## Bill of materials (control wiring)

| Item | Detail | Purpose |
|---|---|---|
| ESP32-S3-WROOM-1 N16R8 DevKit | dual-core LX7, ESP-IDF v5.5, 16 MB flash | controller |
| WP880 ESC | bidirectional brushed | motor drive |
| Steering servo | DS3240 (or equivalent) | rudder/steering |
| RC receiver | configurable failsafe — must output **no PWM** on RF loss | RC input source |
| LiFePO4 12V 100Ah | main pack | supply |
| Buck converter | 12V -> 5V | ESP32 / receiver supply |
| Pull-down R (x2) | ~10 kΩ, GPIO18 and GPIO8 to GND | hold outputs low in dead window |
| Decoupling caps | bulk + local 100 nF near ESP32 / buck output | brownout-sag margin, noise |
| 12V tap | **before the kill-switch** | keeps controller alive for FAILSAFE reporting while drive is cut |
| E-stop / kill-switch | cuts motor drive, leaves ESP32 powered | R13 emergency stop |

## Pin map (fixed — ESP32-S3 N16R8)

GPIO4 <- CH1 (steering) · GPIO5 <- CH2 (throttle) · GPIO6 <- CH4 (diag) ·
GPIO7 <- CH3 (diag) · GPIO16 <- GPS TXD (UART RX) · GPIO21/47 <- IMU SDA/SCL ·
GPIO14/13 <- IMU INT/RST · GPIO18 -> servo · GPIO8 -> ESC · GPIO2 -> status LED ·
common ground across ESP32-S3 / receiver / ESC.

Reserved/unusable on N16R8: GPIO33-37 (octal PSRAM), GPIO26-32 (SPI flash),
GPIO22-25 (do not exist), GPIO19/20 (native USB), GPIO43/44 (UART0 console),
GPIO0/3/45/46 (strapping), GPIO48 (on-board RGB). Full I/O diagram:
`docs/hardware/esp32s3-io-wiring.svg`.

## Measurement checklist

These mirror the "Plan pomiarów sprzętu" in the task file. They require the
physical rig (ESP32, WP880, oscilloscope, receiver) and are tracked as
hardware-gated. Record the measured number next to each item.

### WP880 ESC

- [ ] Feed 1500 us — does it stand still? Determine the actual neutral and band (SI-2).
- [ ] Start threshold forward / reverse.
- [ ] Behaviour on PWM loss (to neutral? after how long?).
- [ ] Behaviour on a frozen-but-valid PWM (~70% LEDC, core stalled).
- [ ] Does it need its own throttle-range calibration (interacts with R15)?
- [ ] Behaviour with neutral applied at boot.
- [ ] Fast full-fwd -> full-rev transition — cutout / current spike / plugging?
      -> decides `reverseNeutralDwellMs`.
- [ ] Acceptance — in DISARMED and after FAILSAFE soft-stop the propeller does
      not spin (SI-2).

### ESP32 outputs (oscilloscope)

- [ ] State of GPIO18/GPIO19 during reset / boot / bootloader.
- [ ] LEDC behaviour after a watchdog reset (glitch?).
- [ ] Brownout-sag — pin state while the supply collapses.
- [ ] Length of the boot dead window (reset -> LEDC re-init) as a number.
- [ ] Confirm pull-down on GPIO19 (ESC line must not float).
- [ ] After boot, GPIO18/GPIO19 emit neutral/center; measure the dead window
      (reset -> first pulse); behaviour under watchdog reset and brownout sag;
      check pull-downs (Unit 2 [HW]).

### RC receiver

- [ ] Behaviour on RF loss (goes silent / hold-last / preset) + receiver
      failsafe configuration. Hard requirement: **no PWM on RF loss.**
- [ ] Measured frame period (feeds the RC_valid period threshold — do NOT
      assume 20 ms).

## Sources

- Requirements: docs/requirements/2026-06-16-kayak-motor-firmware-v1-requirements-v2.md
- Technical plan: docs/plans/2026-06-16-001-feat-kayak-motor-firmware-v1-plan.md
