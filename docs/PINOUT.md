# STC12C5A60S2 reference

🇬🇧 English · [🇩🇪 Deutsch](PINOUT.de.md)

Everything here is taken from the official
[STC12C5A60S2 / STC12LE5A60S2 datasheet](https://www.stcmicro.com/datasheet/STC12C5A60S2-en.pdf)
(STC MCU Limited, revision 2011-07-15). Section numbers refer to that document.

---

## Part variants

| Family | Supply | Notes |
|---|---|---|
| `STC12C5A60S2` | **3.5 – 5.5 V** | The 5 V part. This is what the repo assumes. |
| `STC12LE5A60S2` | **2.1 – 3.6 V** | The low-voltage sibling. **5 V will damage it.** |

Common to both: 60 KB flash, 1280 B RAM (256 B internal + 1024 B auxiliary),
1 KB EEPROM, 2 UARTs, 2 DPTRs, 4 timers, 8-channel 10-bit ADC, SPI,
16-bit PCA / 8-bit PWM ×2, watchdog, 7 wake-up-capable external interrupts.

The `…AD` variants drop one UART and one PCA channel; the `…PWM` variants drop
the ADC. All share the pinout.

## Package → available ports

| Package | I/O count | Port 4 | Port 5 |
|---|---:|---|---|
| **PDIP-40**, QFN-40 | 36 | **P4.4 – P4.7 only** | — |
| PLCC-44, LQFP-44 | 40 | P4.0 – P4.7 | — |
| LQFP-48 | 44 | P4.0 – P4.7 | P5.0 – P5.3 |

## PDIP-40 pin map

| Pin | Name | Pin | Name |
|---:|---|---:|---|
| 1 | P1.0 / ADC0 / CLKOUT2 | 40 | VCC |
| 2 | P1.1 / ADC1 | 39 | P0.0 / AD0 |
| 3 | P1.2 / ADC2 / ECI / RxD2 | 38 | P0.1 / AD1 |
| 4 | P1.3 / ADC3 / CCP0 / TxD2 | 37 | P0.2 / AD2 |
| 5 | P1.4 / ADC4 / CCP1 / SS | 36 | P0.3 / AD3 |
| 6 | P1.5 / ADC5 / MOSI | 35 | P0.4 / AD4 |
| 7 | P1.6 / ADC6 / MISO | 34 | P0.5 / AD5 |
| 8 | P1.7 / ADC7 / SCLK | 33 | P0.6 / AD6 |
| 9 | RST / P4.7 | 32 | P0.7 / AD7 |
| 10 | P3.0 / RxD / INT | 31 | P4.6 / EX_LVD / RST2 |
| 11 | P3.1 / TxD | 30 | P4.5 / ALE |
| 12 | P3.2 / INT0 | 29 | P4.4 / NA |
| 13 | P3.3 / INT1 | 28 | P2.7 / A15 |
| 14 | P3.4 / T0 / INT / CLKOUT0 | 27 | P2.6 / A14 |
| 15 | P3.5 / T1 / INT / CLKOUT1 | 26 | P2.5 / A13 |
| 16 | P3.6 / WR | 25 | P2.4 / A12 |
| 17 | P3.7 / RD | 24 | P2.3 / A11 |
| 18 | XTAL2 | 23 | P2.2 / A10 |
| 19 | XTAL1 | 22 | P2.1 / A9 |
| 20 | GND | 21 | P2.0 / A8 |

## I/O port modes (§4.1, §4.3)

Two registers per port, one bit per pin:

| `PxM1` | `PxM0` | Mode | Behaviour |
|:---:|:---:|---|---|
| 0 | 0 | Quasi-bidirectional | **Reset default.** Sinks ≤20 mA; sources ~230 µA typ. (spec'd 150–250 µA) through a weak pull-up. Also readable as an input. |
| 0 | 1 | Push-pull | Strong pull-up. Sources *and* sinks ≤20 mA. Always add a series resistor. |
| 1 | 0 | Input-only | High impedance, Schmitt-triggered. |
| 1 | 1 | Open-drain | All pull-ups off. Needs an external pull-up. |

Every pin has a Schmitt-triggered input.

**Current limits (§4.6):**
- Per pin: 20 mA sink max
- Per 8-bit port: **80 mA** total sink (not 8 × 20 = 160!)
- Per chip: **150 mA** total sink (all ports combined)

The per-port limit is why you cannot drive 8 LEDs at 20 mA each on one port —
the aggregate is 80 mA, so each LED gets at most 10 mA, or you multiplex.
Source current in quasi-bidirectional mode is only ~230 µA per pin.

### Driving an LED (§4.6)

| Port mode | Wiring | Resistor |
|---|---|---|
| Quasi-bidirectional (default) | **Sink**: `+V ──[R]──▶|── pin` | ≥1 kΩ recommended, 470 Ω absolute minimum |
| Push-pull | **Source**: `pin ──[R]──▶|── GND` | same |

This repo uses **sink** wiring plus **push-pull** mode: sink drive works in any
mode, and push-pull makes the off-state a hard VCC rather than a weak pull-up.

### The 4-clock I/O gotcha (§4.4)

A classic 8051 takes 12 clocks per I/O access; the 1T STC12 takes 4. When you
change a pin and then read an *external* signal back, the instruction has
retired but the outside world has not settled — insert one or two `nop`s.

Also from §4.4: I²C/SPI and other open-drain peripherals want a 10 kΩ pull-up;
an I/O driving a PNP base wants either an external pull-up matched to the base
resistor, or push-pull mode.

### Making a pin low at reset (§4.8)

Out of reset every pin is a weak pull-up, i.e. high. If a pin must be low at
power-on, hang a 1 k/2 k/3 kΩ resistor to GND on it (behind a ≥470 Ω series
resistor). The weak internal pull-up cannot overcome it, so the pin reads low
until your code sets push-pull mode.

## SFR quick reference

Only the registers this repo touches, plus the ones you reach for next.

| SFR | Addr | Bit-addressable | Purpose |
|---|:---:|:---:|---|
| `P0` | 0x80 | ✓ | Port 0 data |
| `P1` | 0x90 | ✓ | Port 1 data |
| `P2` | 0xA0 | ✓ | Port 2 data |
| `P3` | 0xB0 | ✓ | Port 3 data |
| `P4` | 0xC0 | ✓ | Port 4 data (needs `P4SW`, see below) |
| `P5` | 0xC8 | ✓ | Port 5 data (LQFP-48 only) |
| `P0M1` / `P0M0` | 0x93 / 0x94 | ✗ | Port 0 mode |
| `P1M1` / `P1M0` | 0x91 / 0x92 | ✗ | Port 1 mode |
| `P2M1` / `P2M0` | 0x95 / 0x96 | ✗ | Port 2 mode |
| `P3M1` / `P3M0` | 0xB1 / 0xB2 | ✗ | Port 3 mode |
| `P4M1` / `P4M0` | 0xB3 / 0xB4 | ✗ | Port 4 mode |
| `P5M1` / `P5M0` | 0xC9 / 0xCA | ✗ | Port 5 mode |
| `P4SW` | 0xBB | ✗ | Enables P4.4/P4.5/P4.6 as GPIO |
| `AUXR` | 0x8E | ✗ | `T0x12 T1x12 UART_M0x6 BRTR S2SMOD BRTx12 EXTRAM S1BRS` |
| `AUXR1` | 0xA2 | ✗ | `- PCA_P4 SPI_P4 S2_P4 GF2 ADRJ - DPS` |
| `TMOD` | 0x89 | ✗ | Timer 0/1 mode |
| `TCON` | 0x88 | ✓ | `TR0`, `TF0`, `TR1`, `TF1`, INT edge flags |

All of these are declared in SDCC's `<stc12.h>` — `#include <stc12.h>` and you
have them by name. (`board.h` in this repo does that for you.)

### `P4SW` (0xBB) — reset value `x000,xxxx`

| Bit | Name | 0 (default) | 1 |
|:---:|---|---|---|
| 6 | `LVD_P4.6` | pin 31 is the external low-voltage detect / RST2 | pin 31 is GPIO **P4.6** |
| 5 | `ALE_P4.5` | pin 30 is ALE (external data memory) | pin 30 is GPIO **P4.5** |
| 4 | `NA_P4.4` | pin 29 is a weak pull-up with no function | pin 29 is GPIO **P4.4** |

`P4.7` (pin 9) is the reset pin by default and can only be repurposed as GPIO
via the ISP option — and then you must supply an external clock.

### `AUXR1` (0xA2) — moving peripherals to Port 4

`PCA_P4`, `SPI_P4` and `S2_P4` relocate the PCA/PWM, SPI and UART2 pins from
Port 1 to Port 3/4. Useless on PDIP-40 (P4.0–P4.3 are not bonded), but worth
knowing if you move to LQFP.

`DPS` selects between the two data pointers — a genuinely useful STC12 feature
for block copies.

## Clock (§2.1)

* **Internal RC**, used when no crystal is fitted: **11 – 17 MHz at 5 V**,
  8 – 12 MHz at 3 V. Not trimmed from the factory in any useful way for the
  STC12 series — `stcgal -t` only works on STC15 and later.
* **External crystal** between XTAL1 (19) and XTAL2 (18), 22–47 pF to GND from
  each. 11.0592 MHz is the traditional choice because it divides into exact
  UART baud rates.
* **External oscillator**: drive XTAL1, leave **XTAL2 floating**.
* Above 33 MHz, use an active oscillator rather than a passive crystal.

## Reset (§2.3)

* `RST` (pin 9) is **active high**. Hold it high for ≥2 machine cycles.
* Below 12 MHz: a plain **1 kΩ to GND** is the whole reset circuit.
* Above 12 MHz: STC recommends using the secondary reset pin `RST2` (pin 31,
  `P4.6`) instead.
* The `10 kΩ + 10 µF` RC network you see in old 8051 schematics is for
  *active-low* reset parts. Do not copy it here.

## In-system programming (§13)

The bootloader lives in mask ROM and speaks a serial protocol on
`P3.0`/`P3.1`. It runs **only immediately after a cold power-on** — a warm
reset via the RST pin does not re-enter it. That is why every flash operation
needs a genuine power cycle.

`stcgal -P stc12` implements this protocol. The handshake happens at 2400 baud
and then negotiates up to the `-b` rate.
