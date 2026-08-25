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
- **Independent keys: all four measured:** K1=P3.1, K2=P3.0, K3=P3.2,
  K4=P3.3. K1/K2 require both P5 UART shunts to be removed after flashing
  for an isolated test. Vendor wiring two keys onto the serial pins is a
  real conflict, not a doc error — flashing with a key held can fail.
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
  The DVD example's displayed hexadecimal key map, bench-confirmed twice, is:

  ```text
  3  7  B  F       top row
  2  6  A  E
  1  5  9  D
  0  4  8  C       bottom row
  ```

  This is the vendor demo's numbering convention; `src/09-keyshow89` uses
  ordinary row-major numbering instead, so its top row is 0–3.
- **DS18B20: fitted and confirmed on P3.7.** The untouched DVD example 16
  displayed a plausible live `+030.56 C`. The earlier discovery probe's
  “no presence” result was superseded by this peripheral-specific test.
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
- **8×8 DOT MATRIX: FULLY MEASURED AND DRIVEN FROM BLOCKS**
  (11-matrix89 hunts + pseudocode/15-a2-matrix.bw, iterated with the
  owner over four flashes). The wiring: the 74HC595 (silkscreen
  SER=P3.4, RCLK=P3.5, SCLK=P3.6 — labels below the matrix) selects
  the physical ROWS active HIGH with **Q7 = top … Q0 = bottom**; P0's
  bits sink the COLUMNS active LOW with **bit 7 = left** (corner-dot
  probe). Net: image bytes read top-down, MSB-left — literals look
  like the picture. **THE TRAP THAT COST FIVE DARK FLASHES: J24
  (silkscreen GND-OE-VCC) shipped with the cap on OE–VCC, tri-stating
  the 595's outputs entirely.** The cap belongs on OE–GND. Every
  correct-pin firmware was dark until the owner moved it; a jumper,
  not a wire, was the whole mystery. The steady heart at ~110 Hz
  scan (`wait 1 ms` per row) is the shipped demo.
  owner-observed: phase 2). So the LEDs SHARE THE PORT with the 7-seg
  digit select (P2.2–P2.4) — the vendor's "modules can't all be used
  at once" caveat as measured copper: scanning the tubes dances the
  LEDs. `27-a2-ledrow` subsequently confirmed **active-low**, with
  P2.0–P2.7 walking physically D1→D8. The P2.5/D6 step also clicks BZ1,
  another direct confirmation of that shared net.
- **LCD1602 BUS-CONFLICT OBSERVATION (2026-08-24):** with the LCD1602
  inserted and J24 on **OE–GND**, matrix rows 1/3/5/8 light at columns 5–8,
  and D7/D8 in the separate eight-LED row light. Moving J24 to **OE–VCC**
  makes the matrix dark while D7/D8 remain lit. The neighboring 7-segment
  indication also changes from the usual `8.` to `I-` (three segments) when
  the LCD is inserted. Together these are direct evidence that the LCD data
  bus affects the matrix and 7-segment P0 paths, while P2.6/P2.7 LCD control
  activity remains visible on the shared LED row. Leave J24 on **OE–VCC for
  LCD work**; OE–GND is correct only when intentionally driving the matrix.
  LCD control order is the board's printed `RW/RS/EN : P25-P27`, i.e.
  RW=P2.5, RS=P2.6, EN=P2.7. `src/12-lcd89` v4 uses a write-only,
  fixed-delay initialization so RW never lets the LCD drive the shared bus.
  A recovered A2 schematic in
  `../stc-research/corpus/meng-plus_51MCU/频率计设计/4--开发板原理图/`
  confirms J2 pin-for-pin: 1=GND, 2=VCC, 3=VO, 4=RS, 5=RW, 6=EN,
  7–14=DB0–DB7, 15=backlight VCC, 16=backlight GND. It also shows the
  eight 10 kΩ P0 pull-ups and no intervening LCD-enable jumper. Because both
  ends carry GND/VCC pairs, a reversed module can still power its controller
  and backlight while scrambling every control/data connection; powered does
  not prove correctly oriented or correctly aligned seating. **BENCH SOLVED:**
  the module was in fact reversed. Rotating it 180 degrees initialized it
  immediately with the untouched vendor example 18 image. The working
  orientation is visually counterintuitive because the LCD overlaps the MCU
  somewhat; trust pin 1/alignment, not the tidier mechanical appearance.

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

## Recovered schematic pin map and usable combinations (2026-08-24)

The recovered A2 schematic plus the HC6800EM3 DVD and local A2-specific
examples settle the remaining labels:

| module/header | pins | usable notes and conflicts |
|---|---|---|
| XPT/ET2046 12-bit ADC | DIN=P3.4, CS=P3.5, DCLK=P3.6, DOUT=P3.7 | Usable. AIN0 is the onboard 502 pot; AIN1/AIN2 go to the NTC/photoresistor networks; AIN3 is external. Conflicts with DS1302, DS18B20 and matrix-595 control. The HC6800EM3 DVD's example 14 is **not** applicable: it targets that board's PCF8591 on P2.0/P2.1. Use the local `oopxiajun_STC89C52/15-1 AD&DA` XPT2046 driver instead. |
| filtered DAC/PWM output | P2.1 → RC filter + LM358 → DAC1 | Usable as timer/software PWM. Conflicts with the AT24C02's SCL on P2.1. This is separate from the P3.4–P3.7 ADC interface. |
| buzzer BZ1 | **P2.5 on the physical board, shared with LCD RW** | Bench-confirmed by `22-a2-buzzer`: the P1.5 long burst was silent and the P2.5 phase produced two short tones. The recovered schematic's P1.5 assignment is from a different revision or is wrong. Keep LCD E low while sounding BZ1 and restore P2.5 low before LCD writes. |
| DS1302 RTC | IO=P3.4, CE/RST=P3.5, SCLK=P3.6 | Usable with LCD1602; conflicts with XPT2046 and the matrix's 595 controls. DVD example 15 includes an LCD clock image/source. |
| RSTK1 | MCU RST (active high) | Hardware reset only; no application reads it and example 18 does not use it. It restarts the flashed program but does not enter ISP; flashing still needs cold power-on. |
| DS18B20 socket | P3.7 | Usable if a sensor is actually fitted. Keep XPT2046 CS=P3.5 high so its DOUT releases P3.7. DVD example 16 includes LCD temperature demos. |
| IR receiver IR1 | P3.2 / INT0 | Usable with LCD and ideal for interrupt decoding. Shares the net with independent key K3. DVD example 20 decodes the supplied remote and displays its code on LCD. |
| UART jumper **P5** | 1–2: CH340 `RXD-U` ↔ MCU P3.0/RXD; 3–4: CH340 `TXD-U` ↔ MCU P3.1/TXD | Both shunts fitted enables USB serial/ISP, as used on this bench. Remove them to isolate/free P3.0/P3.1. Those pins also share independent keys K1/K2. |
| NRF24L01 2×4 | IRQ=P1.6, MISO=P1.4, MOSI=P1.1, SCK=P1.7, CSN=P1.3, CE=P1.2, plus 3.3 V/GND | Electrically usable through the onboard 470 Ω series network and 3.3 V rail. Conflicts heavily with the P1 matrix keypad; CE/CSN/MOSI also overlap the stepper P1.0–P1.3 group, and whole-P1 writes disturb BZ1. Add local supply decoupling for radio current bursts. |
| J20/J22/J25/J29 breakouts | P1 / P0 / P2 / P3 respectively | Usable as test/access points, but they are not isolated GPIO: the onboard modules below remain hard-wired. |

Hard-wired port ownership summary:

- **P0:** LCD data, 7-segment segment bus, and 8×8 matrix columns.
- **P1:** 4×4 keypad; P1.0–P1.3 stepper; most NRF24L01 signals. The recovered
  schematic's BZ1=P1.5 assignment does not apply to this physical revision.
- **P2:** eight LEDs; P2.0/P2.1 AT24C02; P2.1 DAC; P2.2–P2.4
  74HC138 digit select; P2.5–P2.7 LCD controls; this physical board labels
  BZ1 also shares P2.5 (bench-confirmed).
- **P3:** P3.0/P3.1 UART + K1/K2; P3.2 IR + K3; P3.3 K4;
  P3.4–P3.6 XPT2046/DS1302/595; P3.7 XPT2046 DOUT/DS18B20.

The LCD can therefore remain installed for the best next demonstrations:
DS1302 clock, IR remote code display, DS18B20 temperature (with sensor), or
an A2-specific XPT2046 pot/ADC display. Avoid whole-port assignments to P0,
P1, or P2 when combining modules; use bit writes and explicit chip-select
idle states.

### Supplied IR remote command map (bench-confirmed)

The DVD example 20 decoder works unchanged with IR1 on P3.2/INT0 and shows
the NEC command byte on the LCD. The supplied remote produced:

| key | code | key | code | key | code |
|---|---:|---|---:|---|---:|
| Power | `45h` | Mode | `46h` | Mute | `47h` |
| Play | `44h` | Back | `40h` | Fast-forward | `43h` |
| EQ | `07h` | Vol- | `15h` | Vol+ | `09h` |
| 0 | `16h` | Repeat | `19h` | U/SD | `0Dh` |
| 1 | `0Ch` | 2 | `18h` | 3 | `5Eh` |
| 4 | `08h` | 5 | `1Ch` | 6 | `5Ah` |
| 7 | `42h` | 8 | `52h` | 9 | `4Ah` |

Do not use independent key K3 while decoding IR: it shares P3.2 and therefore
looks like receiver activity to the external-interrupt handler.

### DS1302 bench result

The DVD example 15 worked unchanged: its forced `2013-01-01 12:00:00` value
displayed and counted, confirming the DS1302, oscillator and P3.4–P3.6 bus.
A subsequently flashed read-only image initially found partly invalid BCD;
after a full off/on cycle it found broadly invalid registers and no counting.
Therefore this board's RTC backup retention is **not working**. Check that the
backup cell is present, has voltage, has the correct polarity and contacts the
holder. This does not invalidate the powered DS1302 test.

### ET/XPT2046 ADC bench result

The A2-specific `24-a2-xpt2046` firmware confirms the P3.4–P3.7 serial bus and
the four 12-bit command selections. With 16-sample averaging:

- AIN0 (`P`) follows the onboard 502 potentiometer.
- AIN1 (`N`) is the NTC path: approximately 670–760 at ambient, falling to
  about 300 when warmed by a finger.
- AIN2 (`L`) remains near 2050 under cover and illumination. The divider is
  biased, but GR1/photoresponse is not yet working; inspect the component and
  connection before treating this channel as a light sensor.
- AIN3 (`X`) remains near 785 rather than floating. The schematic brings AIN3
  beside DAC1 at J52. Bench inspection found J52 unshunted with pins labelled
  `IN3` and `DAC`, so that value was merely a floating-input reading. A shunt
  across J52 is the intended closed-loop DAC-to-ADC test connection.

### DS18B20 bench result

The untouched DVD example 16 works with the fitted sensor on P3.7 and displays
a plausible live value (`+030.56 C` observed). This confirms the sensor, its
one-wire bus and the board pull-up. Keep ET/XPT2046 CS=P3.5 high whenever the
DS18B20 owns shared P3.7.

### DAC1 loopback bench result

With J52 shunted from `DAC` to `IN3`, `25-a2-dac-loopback` stepped P2.1 PWM
through 0/25/50/75/100 percent and AIN3 produced a repeatable monotonic
staircase. Observed levels across repeated cycles were approximately 306–339,
820–857, 1447–1628, 1670–1748 and 2140–2142. This confirms P2.1 PWM, the RC
filter, LM358 path, J52 and AIN3. The numerical span is not yet voltage-
calibrated. With J52 open, the earlier AIN3 values were floating and invalid.

### AT24C02 and independent-key bench result

`26-a2-eeprom-keys` passed. It saved EEPROM addresses `F8h`–`FBh`, wrote the
patterns `A5h 5Ah 3Ch C3h`, ACK-polled each internal write cycle, read and
verified every pattern, restored all four original bytes, and read-verified
the restoration. Thus P2.0/SDA, P2.1/SCL and AT24C02 byte read/write work. The
test is nondestructive but does not separately measure long-term power-off
retention.

With both P5 UART shunts removed after flashing, K1–K4 were also confirmed on
P3.1, P3.0, P3.2 and P3.3 respectively. RSTK1 successfully restarts the MCU in
hardware; it remains a reset input rather than an application key.

## BrickWright consequences

The bench map is now reflected in BrickWright's `board-prechin-a2` preset and
its first-class `LCD1602` declaration. The A2 LCD form is:

```text
PART lcd = LCD1602 DATA P0.4 P0.5 P0.6 P0.7 RS P2.6 RW P2.5 EN P2.7
```

A generic 8051 build can omit RW with `WRITE ONLY`. The maintained examples
are `80-a2-lcd-moving-text`, `81-8051-lcd1602-parallel`, and
`82-a2-led-row`; examples 78 and 79 now use the measured keypad map and the
real P2.2–P2.4 decoded display select. The preset starts J24 in its LCD-safe
OE–VCC position. Matrix examples must deliberately switch J24 to OE–GND.

The LED row and the eight-digit display are separate teaching configurations,
not independent simultaneous outputs: the display continually changes
P2.2–P2.4 and a whole-port LED write destroys that select value. Likewise,
P2.5 buzzer activity while the LCD is installed drives LCD RW and may click
the buzzer during LCD reads or poorly isolated writes.
