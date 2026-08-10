# Bench runbook — one session, four answers

Everything in this project has been verified under emulation (category 2b) and
**nothing has run on silicon**. This document turns limited bench time into
category-1 evidence. Follow it in order; each step either unblocks the next or
fails cheaply.

**What you need:** an STC12C5A60S2 board (PDIP-40 or LQFP-44), a USB-TTL
adapter (CH340 or CP2102), a 10 kΩ potentiometer, two LEDs (any colour), two
resistors (1 kΩ), a multimeter. A scope is useful for BENCH-PWM but not required.

**Time budget:** 30 minutes covers BENCH-ADC and BENCH-PWM (same wiring).
BENCH-CUBE needs the cube kit. BENCH-UART needs a second serial path.

---

## Collisions — read before wiring anything

Three pins serve double duty. Getting this wrong costs the session.

| pin | chip pin # (PDIP-40) | conflict |
|---|---|---|
| **P1.3** | 24 | ADC channel 3 AND PCA CCP0 (servo). Do not wire both. |
| **P3.0 / RxD** | 10 | UART1 receive AND ISP download. The bootloader enters only on **cold power-on** — a reset button will not do it. Unplug VCC, replug, then `stcgal` talks. |
| **P3.1 / TxD** | 11 | UART1 transmit AND ISP upload. Same constraint. |

**Flashing rule:** disconnect anything on P3.0/P3.1 before flashing. After the
program starts, reconnect. The bootloader and the monitor cannot share the line.

---

## 1. BENCH-ADC — the analog path (do this first)

**Why first:** `src/02-adc` is ready to flash. It tests flashing AND the ADC in
one step. If flashing fails, nothing else works; stop here and debug the
adapter.

### Wiring (by chip pin number, PDIP-40)

| chip pin | name | connect to |
|---|---|---|
| 40 | VCC | +5 V |
| 20 | GND | ground |
| 24 | P1.3 (ADC3) | pot wiper |
| — | pot end A | +5 V |
| — | pot end B | GND |
| 21 | P1.0 | LED1 anode via 1 kΩ from VCC (active-low) |

### Flash

```bash
make EXAMPLE=02-adc flash
```

If `stcgal` does not connect: unplug VCC, run the command, then plug VCC in.
The bootloader needs a **cold** power-on.

### Pre-registered prediction (category 2b)

The program reads P1.3 (10-bit ADC), scales the result, and uses it as a delay.

| pot position | ADC reading | blink period | source |
|---|---|---|---|
| wiper → GND | ~0 | ~100 ms (fastest) | `STC12-PERIPHERAL-MODEL.md` §4, `ucsim-stc` `c0bd557` |
| wiper → VCC | ~1023 | ~2000 ms (slowest) | same |
| **ratio** | — | **~20:1** | derived from loop constant |

**Tracking** should be monotonic and approximately linear across the pot's travel.

### What to observe

- The blink period at both extremes (two numbers, in ms — use a stopwatch or count 10 blinks)
- Whether tracking is smooth or has jumps/plateaus
- Whether the rate is stable when the pot is left alone (drift = floating input)

### Decision rule

| observation | verdict |
|---|---|
| Ratio between extremes is 10:1 – 30:1, tracking smooth | **PASS** — analog path works |
| Ratio is 10:1 – 30:1 but a plateau or jump exists at a specific position | **INCONCLUSIVE** — may indicate a stuck ADC bit; record the position |
| Ratio is ~1:1 (constant blink at both extremes) | **FAIL** — ADC not reading. Suspect: wrong pin, P1ASF not set, pot wiring swapped |
| LED does not blink at all | **FAIL** — program did not flash, or LED wiring wrong |

**Tolerance on absolute periods:** ±30%. The ratio is the stronger test.

### What it upgrades

- **ADC register sequence** moves from 2b to **category 1** (silicon confirms the
  emulator-verified sequence).
- **ADC analog path** moves from "open" to "confirmed on silicon".
- The `⚠ UNVERIFIED ON SILICON` warning in `STC12-PERIPHERAL-MODEL.md` §4 can
  be removed for the register sequence; the analog accuracy stays open until
  measured with a known voltage source (not a pot).

### What it does NOT upgrade

- ADC **accuracy** (10-bit linearity, offset, gain) — a pot test cannot measure
  these. That would need a calibrated voltage source and a DAC comparison.
- Anything about **PWM, UART, or the cube** — different peripherals, different tests.

---

## 2. BENCH-PWM — LED current at 50% duty (same wiring, add a meter)

**Why second:** the LED and resistor from BENCH-ADC are already wired. Just add
the multimeter in series.

### Wiring change

Disconnect the pot from pin 24 (P1.3). Flash the PWM test program:

```bash
make EXAMPLE=03-pwm flash
```

Reconnect pin 21 (P1.0) through a **multimeter in DC mA mode** in series with
the 1 kΩ resistor and LED.

### Pre-registered prediction (category 2b)

| condition | predicted current | source |
|---|---|---|
| 50% duty, 1 kΩ + LED (Vf=2.0V), VCC=5V | **1.46 mA** DC average | `bw-board` brightness model, `ucsim-stc` `1d3c932` |
| 100% on (steady LOW) | **2.93 mA** | I = (5−2)/(1000+25) |

Derivation: at 50% duty the pin is LOW half the time (2.93 mA) and HIGH the
other half (~0 mA through the LED). A DC multimeter reads the average: 1.46 mA.

### Decision rule

| reading | verdict |
|---|---|
| 1.17 – 1.76 mA (±20%) | **PASS** — duty cycle and pin drive are correct |
| 0.5 – 1.17 mA or 1.76 – 2.5 mA | **INCONCLUSIVE** — duty may be off, or LED Vf differs; record the value |
| < 0.1 mA | **FAIL** — PWM not running, or pin not driving |
| ~2.9 mA (steady, no flicker) | PWM is stuck ON — the PCA is not toggling |

### What it upgrades

- **PCA 8-bit PWM duty cycle** moves from 2b to category 1 if the current
  matches the prediction.
- The `BENCH-PWM` ID can be closed.

---

## 3. BENCH-CUBE — the voxel map and polarity

**Needs:** the 4×4×4 LED cube kit, a separate session.

### Flash

```bash
sdcc --iram-size 256 -o build/ src/20-ledcube/probe.c
make EXAMPLE=20-ledcube flash
```

### Pre-registered prediction

**Polarity: active-HIGH.** At `(select=0xFE, data=0x01)`:
- **One LED lights** → P0 is active-high, `BW_CUBE_ACTIVE_HIGH = 1`. This is
  what all four codebases assume (`emu8051-stc`, `ucsim-stc`, `sb3-creator`,
  `bw-circuit-ui`), derived from the vendor animation tables.

**Refresh: 124 Hz** (invisible to the eye). From the scan timing: 8 lines × ~1 ms
dwell. Measured under emulation (`ucsim-stc` `1d3c932`).

### What to observe

1. At `(FE, 01)`: does **one** LED light, or do **seven** light (one dark)?
2. Walk through all 64 positions and fill `src/20-ledcube/README.md`'s table.
3. Is there visible flicker? (There should not be.)

### Decision rule

| observation | verdict |
|---|---|
| One LED lights at `(FE, 01)` | **PASS** — active-high confirmed |
| One LED DARK, seven lit at `(FE, 01)` | **PASS but inverted** — active-low; flip `BW_CUBE_ACTIVE_HIGH` in 4 files |
| No LED lights at all | **FAIL** — wiring, or P0/P2 port mode wrong |
| Visible flicker | **INCONCLUSIVE** — dwell shorter than predicted; record the apparent rate |

---

## 4. BENCH-UART — the monitor on a real UART

**Needs:** a second USB-TTL adapter (the first is for ISP).

### Wiring

| chip pin | name | connect to |
|---|---|---|
| 10 | P3.0 / RxD | adapter TXD |
| 11 | P3.1 / TxD | adapter RXD |

**Collision:** these are the ISP pins. Disconnect the monitor adapter before
flashing; reconnect after. The bootloader needs cold power-on.

### Flash

```bash
make EXAMPLE=10-live-firmware flash
```

Then reconnect the monitor adapter and run:

```bash
python3 tools/live-monitor.py --port /dev/cu.usbserial-XXXX
```

### Pre-registered prediction

| command | expected | source |
|---|---|---|
| `HELLO` | a valid framed reply | wire format verified by 4 codecs |
| `POS` | a position matching program state | `ucsim-stc` `644c5c6` |
| 500 ms halt | wall-clock **500 ± 50 ms** | emulator measured 527 ms; silicon should be closer to 500 |

### Decision rule

| observation | verdict |
|---|---|
| `HELLO` answers with a valid frame | **PASS** — baud rate, framing, and codec are correct on silicon |
| `HELLO` answers garbled | **INCONCLUSIVE** — baud mismatch; suspect FOSC trim |
| No response | **FAIL** — wiring, adapter, or baud. Check P3.0/P3.1 connections |
| Halt skew 480–550 ms | **PASS** — timer accuracy confirmed |
| Halt skew > 600 ms | **INCONCLUSIVE** — host-side latency; record the value |

---

## What to bring back

Four results, keyed by ID. For each: the raw observation (what the meter read,
what the LED did), not a conclusion. A conclusion can be re-derived; a
measurement cannot be re-taken.

| ID | what to record |
|---|---|
| BENCH-ADC | two blink periods (ms) at pot extremes; smooth or not |
| BENCH-PWM | DC mA through the LED at 50% duty |
| BENCH-CUBE | 64-row `(select, bit) → (x, y, z)` table; what happened at `(FE, 01)` |
| BENCH-UART | whether `HELLO` answered; halt skew in ms |

**If something disagrees with the emulators, that is the most valuable result of
the session.** Write it down before explaining it.
