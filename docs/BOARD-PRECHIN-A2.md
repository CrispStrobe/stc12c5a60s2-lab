# Prechin 普中51-单核-A2 — on the bench (2026-08-17)

The owner's second real board, connected the same evening the minimum-system
board delivered first silicon. **This is the BENCH page: measured facts.**
The BOM, the modeled-parts gap list (closed 2026-08-14), and the board-preset
plan live in docs/ROADMAP.md ("MAXIMAL BOARD: PRECHIN 普中51 A2", ~line 1229);
the vendor page + photos are archived in `../stc-research/corpus/prechin-a2`
(LOCAL, no port-map harvest yet — measurement is the only pin authority).

**SETTLED BY EXPERIMENT (07-sevenseg89, owner-observed):** the 7-seg is
**COMMON CATHODE** — `P0=0x3F` walked zeros across the tubes, `P0=0xC0`
drew the g-segment dashes, exactly the CC signature. The vendor table
(共阴) was right; the ROADMAP block's "common-anode" was wrong and is
now corrected. Same experiment confirmed the **74HC138 digit select on
P2.2–P2.4** (the zeros marched digit to digit) and **segments on P0**
through the 74HC245.

## Measured so far (authority: the chip, via discover89)

- **MCU: STC89C52RC/LE52RC** — same chip, magic F002, BSL 6.6C, crystal
  reads 11.030 MHz, 12T. The two boards are twins under the hood; every
  `make PART=stc89c52rc` artifact runs on both.
- **Independent keys: P3.2 and P3.3 measured** (press/release seen). The
  other two never report — consistent with the A2 convention K1=P3.1,
  K2=P3.0: those are the UART/ISP pins, which our discovery firmware
  deliberately masks. Vendor wiring two keys onto the serial pins is a
  real conflict, not a doc error — flashing with a key held would fail.
- **4×4 matrix: FULLY MEASURED** by `src/06-matrix89` (active scan,
  follower pairs vs boot baseline). Rows top→bottom **P1.7, P1.6,
  P1.5, P1.4**; columns left→right **P1.3, P1.2, P1.1, P1.0** — the
  whole P1 port. Verified at six points: corners S1/S4/S13/S16
  measured first, then the interpolated middles CONFIRMED as
  predictions (S6 = P1.6~P1.2, S11 = P1.5~P1.1). Every press and
  release seen symmetrically in both scan directions. Matches the
  byr-51-electronicbalance row/col sbits exactly.
  (Method note, proven live: a matrix is invisible to a passive pin
  watcher — row-to-col contact, both quasi-high — which is why v1
  logged nothing; active scanning is mandatory.)
- **DS18B20: no presence pulse** on any probeable pin so far — socket
  likely unpopulated (check the three-hole footprint next to label 14).
- **BLOCKS-TO-SILICON, WITH A KEYPAD (pseudocode/14-a2-keyshow.bw):**
  the keyshow demo rewritten as pseudocode on the dialect's new
  `PART KEYPAD4X4` compiled (locally AND via the hosted service),
  flashed, and owner-verified 0–F on the tube with the dialect's own
  `key`/`n` prints on the wire. The first attempt flashed clean and
  did NOTHING — which uncovered a real dialect bug: `not` bound
  tighter than `=`, so `IF not k = shown` compared a boolean to a
  number. Fixed at the parser (Python precedence, stc-compiler
  c34ad1b, regression-tested); the bench found in silence what no
  green test had.
- **8-LED module: measured on P2** (08-ledfind89 three-phase test,
  owner-observed: phase 2). So the LEDs SHARE THE PORT with the 7-seg
  digit select (P2.2–P2.4) — the vendor's "modules can't all be used
  at once" caveat as measured copper: scanning the tubes dances the
  LEDs. Per-bit polarity not yet observed (family convention says
  active low; unconfirmed).

## Official module list (from the vendor photos)

stepper header (5-wire 4-phase) · passive buzzer · DS1302 RTC · ADC/DAC =
**XPT2046 + LM358** · reset module · power switch · **CH340C** · MiniUSB ·
AMS1117-3.3V + 5V/3.3V power-out header · 1×4 independent keys ·
**NRF24L01 header (2.4 GHz)** · STC89C52 socket with ALL IO broken out ·
DS18B20 socket · IR receiver · 4×4 matrix keypad · 8 LEDs · **8×8 dot
matrix** · 1× 74HC595 · LCD12864 header · LCD1602 header · dynamic 7-seg:
**74HC245 driving 2×4 digits, common CATHODE** · AT24C02 · 74HC138.

Polarity note: common-cathode 7-seg (drive segments HIGH) — the opposite
of the minimum-system board's common-anode tubes. Third polarity flip
found in three boards; per-board measurement stays the law.

## A2 vs HC6800-EM3 V3.0 ("big brother", not yet on the bench)

| | A2 (here) | EM3 V3.0 |
|---|---|---|
| wiring | fixed traces — vendor's own caveat: "модules can't all be used at once" (port sharing is hard-wired) | modules CABLED to port headers — conflicts resolved by re-plugging |
| dot matrix | 8×8, one 74HC595 | 16×16, cascaded 595 chain |
| ADC/DAC | XPT2046 (SPI, 12-bit) + LM358 | PCF8591 (I2C, 8-bit) + NTC/LDR/trimpot bank |
| wireless | NRF24L01 header | — |
| RS232/RS485 | — | MAX232 + MAX485 |
| relay / NE555 / 74HC165 | — | present |
| motors | stepper header only | ULN2003D DC + UDN2916 stepper drivers on board |
| displays | LCD1602 + LCD12864 headers | same, plus 3.2" TFT touch socket |
| MCU flexibility | 51 only (vendor: no AVR/STM32) | ZIF + STM32/AVR variants documented |
| 7-seg | 8 digits, 74HC245 segments + 74HC138 select, polarity disputed (see above) | 8 digits, 74HC138 select |

Shared core across both (and the tiny board): STC89C52-class chip, CH340,
11.0592 MHz, cold-power-on ISP, buzzer near P1.5, DS1302, AT24C02 I2C on
P2.0/P2.1, IR on P3.2 colliding with a key/INT0.

## What the A2 uniquely offers BrickWright

1. **XPT2046 as ADC** — an SPI peripheral giving the ADC-less STC89 a
   12-bit analog path (vs the EM3's I2C PCF8591). A second SPI consumer
   after the 595, and a better one than the EM3's for precision.
2. **NRF24L01 header** — a 2.4 GHz radio path; long-game candidate for
   chip-to-chip pseudocode messaging.
3. **8×8 dot matrix через 595** — exactly the shift-register story
   `09-shift-register.bw` already tells, now with a display payoff.
4. The hard-wired port conflicts ARE the pedagogy: the vendor's "can't
   use all modules at once" is our PIN/PART conflict modeling, on silicon.
