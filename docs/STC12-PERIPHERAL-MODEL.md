# The STC12C5A60S2 peripheral model — one contract, several implementations

**Why this file exists.** At least three things need to agree on what this chip does:

| implementation | licence | where it may live |
|---|---|---|
| a ucsim fork with an STC12 model | **GPL-2** (part of SDCC) | CI / developer oracle, server-side. **Never bundled.** |
| an emu8051 fork with an STC12 model | **MIT** | bundleable — WASM in the browser, which is what `brickwright-lite` exists for |
| the simulator's board layer | ours, MIT | `sb3-creator/reference/simulation.md` |

If each writes its own model, the work is done three times *and* we end up with three
different answers to "what does this chip do", which is worse than having one. **This document
is the single answer. Implementations cite it; they do not re-derive it.**

## Sources, and what is a fact versus what must still be read

**Register addresses are facts** — take them from here. They are cross-checked between SDCC's
`mcs51/stc12.h` and `stc-compiler/stc_disasm.py`'s `SFR` table (which was itself built against
that header), and they agree.

**Bit-level layouts and timings marked ⚠ are NOT yet verified here.** Read them from the
datasheet and *cite the section* when you fill them in. Do not guess, and do not copy them
from a tutorial: a great many "STC12" tutorials are AT89C51 text with the part number swapped,
and they get P0, ALE, PSEN and EA wrong.

- Datasheet: <https://www.stcmicro.com/datasheet/STC12C5A60S2-en.pdf> (2011-07-15)
- `docs/PINOUT.md` — pin map, SFR and port-mode reference
- `stc-compiler/stc_pseudocode.py` — `PARTS`, and `emit_c`, which shows exactly which
  registers generated code touches. That set is the **minimum** a model must get right.

## 1. Core and timing — get this right first

The single most consequential behaviour, and the one a plausible-looking model gets wrong
silently.

- **1T core.** Instructions take 1–6 clocks, not the classic 12. I/O access takes 4 clocks;
  insert a `nop` or two before reading an external signal back.
- **`AUXR` (0x8E) bit 7 selects Timer 0's clock.** ⚠ *Confirm the bit name and polarity in the
  datasheet.* The generated code clears it:
  - `AUXR.7 = 0` → Timer 0 counts at **FOSC/12** (the 12T rate). This is the reset default and
    what everything we generate relies on.
  - `AUXR.7 = 1` → Timer 0 counts at **FOSC** (1T). A program written for 12T then runs ~12×
    too fast.
- **Everything the toolchain generates is timed off Timer 0, mode 1, at FOSC/12** — the one
  mode a 12T STC89 and a 1T STC12/STC15 count *identically*. That is what makes one program
  correct on either chip, and it is why **no generated code ever contains a cycle-counted
  delay loop** (README §8.1: a 1T part runs one 6–12× too fast — the classic drop-in-socket
  bug).
- The 1 ms reload the emitter uses:

  ```
  T0_RELOAD = 65536 − (FOSC_HZ / 12 / 1000)
  ```

  At `FOSC = 11059200` that is `65536 − 921 = 64615` (0xFC67). **A conforming model must make
  this tick at 1.000 ms with `AUXR.7 = 0`, and at ~0.083 ms with `AUXR.7 = 1`.** Test both
  states explicitly — a model that only ever sees `AUXR.7 = 0` will look correct on our
  programs and be wrong on everything else.
- Internal RC oscillator is 11–17 MHz at 5 V and drifts with temperature. A model should let
  FOSC be configured, and should not pretend the internal RC is exact.

## 2. SFR map

Cross-checked, safe to rely on. `stc-compiler/stc_disasm.py` is the machine-readable copy.

| addr | name | addr | name | addr | name |
|---|---|---|---|---|---|
| 0x80 | P0 | 0x9D | **P1ASF** | 0xC0 | P4 |
| 0x81 | SP | 0xA0 | P2 | 0xC8 | P5 |
| 0x82 | DPL | 0xA2 | AUXR1 | 0xC9 | **P5M1** |
| 0x83 | DPH | 0xA8 | IE | 0xCA | **P5M0** |
| 0x87 | PCON | 0xA9 | SADDR | 0xD0 | PSW |
| 0x88 | TCON | 0xB0 | P3 | 0xD8 | **CCON** |
| 0x89 | TMOD | 0xB1 | **P3M1** | 0xD9 | **CMOD** |
| 0x8A | TL0 | 0xB2 | **P3M0** | 0xDA | **CCAPM0** |
| 0x8B | TL1 | 0xB3 | **P4M1** | 0xDB | **CCAPM1** |
| 0x8C | TH0 | 0xB4 | **P4M0** | 0xE0 | ACC |
| 0x8D | TH1 | 0xB6 | IP2H | 0xE9 | **CL** |
| 0x8E | **AUXR** | 0xB7 | IPH | 0xF0 | B |
| 0x90 | P1 | 0xB8 | IP | 0xF2 | **PCA_PWM0** |
| 0x91 | **P1M1** | 0xB9 | SADEN | 0xF3 | **PCA_PWM1** |
| 0x92 | **P1M0** | 0xBB | **P4SW** | 0xF9 | **CH** |
| 0x93 | **P0M1** | 0xBC | **ADC_CONTR** | 0xFA | **CCAP0H** |
| 0x94 | **P0M0** | 0xBD | **ADC_RES** | 0xFB | **CCAP1H** |
| 0x95 | **P2M1** | 0xBE | **ADC_RESL** | | |
| 0x96 | **P2M0** | 0x98 | SCON | 0x99 | SBUF |
| 0x97 | CLK_DIV | 0x9A | S2CON | 0x9B | S2BUF |
| | | 0x9C | BRT | | |

**Bold = STC-specific, absent from a generic 8051/8052 model.** Those are the additions.

Bit names a model must expose (all standard 8051 except where noted): `TR0` 0x8C, `TF0` 0x8D,
`ET0` 0xA9, `EA` 0xAF, `IT0` 0x88, `IE0` 0x89. Port bits are `Pn_m`.

## 3. Port modes — the part the circuit simulator depends on

Each pin's mode comes from one bit in `PxM1` and one in `PxM0`:

| `PxM1` | `PxM0` | mode | electrical behaviour |
|---|---|---|---|
| 0 | 0 | **quasi-bidirectional** (reset default) | strong pull-down, *weak* pull-up. Also readable. |
| 0 | 1 | **push-pull** | strong both ways |
| 1 | 0 | **input-only** (high impedance) | drives nothing |
| 1 | 1 | **open-drain** | strong pull-down, no pull-up at all |

**The sink/source asymmetry is the central electrical fact of this chip, and the reason for
the whole active-low convention:** a quasi-bidirectional pin **sinks 20 mA but sources only
~230 µA** (datasheet §4.6). So LEDs are wired `+5 V → 1 kΩ → LED → pin` and writing a `0`
lights them.

**For the board layer, model each mode as a Thévenin equivalent** — this is what lets the
simulator *explain* active-low wiring rather than assert it:

| mode | driving 0 | driving 1 |
|---|---|---|
| quasi-bidirectional | ≈ 0 V, low source impedance (sinks 20 mA) | ≈ VCC through a **large** resistance (~20 kΩ scale, i.e. ~230 µA) ⚠ derive the figure from the datasheet's source-current curve |
| push-pull | ≈ 0 V, low impedance | ≈ VCC, low impedance |
| input-only | — | — (high-Z; the external network alone sets the node) |
| open-drain | ≈ 0 V, low impedance | high-Z (needs an external pull-up) |

⚠ The exact on-resistances should be fitted to the datasheet's V–I curves. Order-of-magnitude
correctness is enough to teach the lesson; precision is not.

Package note: only **P4.4–P4.7** exist on PDIP-40. `P4SW` (0xBB) selects the alternate
functions on some P4 pins ⚠ (fill in from the datasheet).

## 4. The ADC — ⚠ AND STILL UNVERIFIED ON SILICON

**Read this before implementing it.** The register sequence below was written from the
datasheet and has **never been confirmed on hardware**. `src/02-adc` in this repo is the
ready-to-flash check and has not been run. An emulator model written from the same datasheet
can show the sequence is *self-consistent*; **it cannot confirm the sequence is right.** If
you implement this, say plainly which of the two you have demonstrated.

What the emitter does (`stc_pseudocode.emit_c`), which a model must support:

```c
P1ASF = <mask of analog pins>;   /* 0x9D — select analog function on P1 */
P1M1 |=  mask;  P1M0 &= ~mask;   /* high-impedance input */
ADC_CONTR = 0xE0;                /* power on, fastest conversion */
...
ADC_CONTR = 0xE8 | channel;      /* power | speed | START | channel */
for (settle = 0; settle < 8; settle++) ;
while (!(ADC_CONTR & 0x10)) ;    /* wait for ADC_FLAG */
ADC_CONTR &= ~0x10;              /* clear the flag BY SOFTWARE */
result = ((unsigned int)ADC_RES << 2) | (ADC_RESL & 0x03);   /* 10-bit */
```

`ADC_CONTR` (0xBC) bit layout, consistent with the constants above:

| bit | 7 | 6 | 5 | 4 | 3 | 2–0 |
|---|---|---|---|---|---|---|
| | ADC_POWER | SPEED1 | SPEED0 | ADC_FLAG | ADC_START | CHS2–CHS0 |

- `0xE0` = power on, `SPEED = 11`, no start. `0xE8 | ch` = the same plus `START` and a channel.
- **`ADC_FLAG` is cleared by software, never by hardware** — that is the trap.
- **ADC channel *n* is physically P1.*n***. There is no mux to any other port; the pseudocode
  front end rejects `ANALOG` on anything but P1.
- `SPEED1:SPEED0` → conversion time in oscillator clocks (datasheet §10.5):
  `00` = 420, `01` = 280, `10` = 140, `11` = 70. The emitter uses `11` (fastest).
  ⚠ Implemented in emu8051-stc — which is *not* verification: a model written from this
  datasheet cannot confirm this datasheet. Cross-check against an independent source (STC's
  own example code, or a second datasheet revision) before relying on the absolute numbers.
- Result alignment is controlled by `AUXR1.ADRJ` (bit 2 of 0xA2, reset value 0). **The bit
  position is now confirmed by a second source**: datasheet §10.2 (p. 290) prints the full
  `AUXR1` layout as `– PCA_P4 SPI_P4 S2_P4 GF2 ADRJ – DPS` while documenting the PCA. The
  *position* is therefore a fact; the *behaviour* below is still datasheet-only.
  ⚠ Note it moves on the STC15 — see `STC15-PERIPHERAL-MODEL.md` §2.1.
  - **ADRJ = 0** (default): `ADC_RES` = high 8 bits, `ADC_RESL[1:0]` = low 2 bits.
    `result = (ADC_RES << 2) | (ADC_RESL & 0x03)` — this is what the emitter generates.
  - **ADRJ = 1**: `ADC_RESL` = low 8 bits, `ADC_RES[1:0]` = high 2 bits.
    `result = (ADC_RES << 8) | ADC_RESL`.
  Both modes implemented in emu8051-stc with unit tests (test_stc12.c, test_adc_edges).
  Datasheet §10.3–10.4. **Not confirmed on silicon** — self-consistent with the datasheet only.
- For the simulator: the conversion input is a **voltage 0…VCC**, mapped linearly to 0…1023.
  That is the whole coupling to the board layer.

## 5. The PCA / PWM block

Filled in from datasheet §10 (p. 290–301). Bit layouts below are the datasheet's, not
inferred. **Two modules on this part** — the datasheet opens §10 with "a special 16-bit Timer
that has **two** 16-bit capture/compare modules". The STC15 has three (`STC15-PERIPHERAL-MODEL.md`
§3), so a model shared between them must gate the third.

### 5.1 Registers, with bit layouts

| reg | addr | b7 | b6 | b5 | b4 | b3 | b2 | b1 | b0 | reset |
|---|---|---|---|---|---|---|---|---|---|---|
| `CCON` | 0xD8 | `CF` | `CR` | – | – | – | – | `CCF1` | `CCF0` | `00xx,xx00` |
| `CMOD` | 0xD9 | `CIDL` | – | – | – | `CPS2` | `CPS1` | `CPS0` | `ECF` | `0xxx,0000` |
| `CCAPM0` | 0xDA | – | `ECOM0` | `CAPP0` | `CAPN0` | `MAT0` | `TOG0` | `PWM0` | `ECCF0` | `x000,0000` |
| `CCAPM1` | 0xDB | – | `ECOM1` | `CAPP1` | `CAPN1` | `MAT1` | `TOG1` | `PWM1` | `ECCF1` | `x000,0000` |
| `PCA_PWM0` | 0xF2 | – | – | – | – | – | – | `EPC0H` | `EPC0L` | `xxxx,xx00` |
| `PCA_PWM1` | 0xF3 | – | – | – | – | – | – | `EPC1H` | `EPC1L` | `xxxx,xx00` |

Counter and compare registers, all plain 8-bit: `CL` 0xE9, `CH` 0xF9 (the shared timer);
`CCAP0L` **0xEA**, `CCAP0H` 0xFA; `CCAP1L` **0xEB**, `CCAP1H` 0xFB.

> The two `CCAPnL` registers were **missing from this document's earlier register list**, and
> therefore from `include/live-sfr.h`'s curated window. They are the ones that carry the live PWM
> duty, so a debugger without them could not read the duty cycle at all.

`CF` is set by hardware on counter overflow and **cleared only by software** — the same trap as
`ADC_FLAG` in §4. `CR` runs the counter. `CCFn` are the per-module match/capture flags, also
software-cleared.

**`AUXR1` (0xA2) is confirmed by this chapter**: `– PCA_P4 SPI_P4 S2_P4 GF2 ADRJ – DPS`. That
is a second, authoritative source for **`ADRJ` at bit 2**, which §4 previously carried on one
source. `PCA_P4` at bit 6 moves the PCA pins — see §5.4.

### 5.2 Clock source — `CMOD.CPS2:CPS1:CPS0`

| CPS2:1:0 | source |
|---|---|
| `000` | SYSclk/12 |
| `001` | SYSclk/2 |
| `010` | **Timer 0 overflow** — the only way to get an adjustable PWM frequency |
| `011` | external clock on `ECI`/P1.2 (max SYSclk/2) |
| `100` | SYSclk |
| `101` | SYSclk/4 |
| `110` | SYSclk/6 |
| `111` | SYSclk/8 |

`CIDL` gates the counter in idle mode. `ECF` enables the overflow interrupt.

**The PWM period is 256 PCA clocks**, because the comparator runs against the 8-bit `CL`. So at
FOSC = 11.0592 MHz: `100` gives 43.2 kHz, `000` gives 3.6 kHz. Both are far above flicker, so
either is fine for LED dimming — but **a buzzer needs a chosen audible frequency, and the only
route to one is `010`, clocking the PCA from Timer 0 overflow.** That is the difference between
`ledBrightness` and `buzzerTone` in the board contract, and it is a codegen decision, not a
board one.

### 5.3 PWM mode — the mechanism, and its polarity trap

Enabled by setting `ECOMn` and `PWMn` in `CCAPMn` (`0x42`; the datasheet's own diagram shows
exactly that bit pattern). **All modules share one frequency** — there is one PCA timer — and
each has an independent duty.

The comparator is **9-bit**: `{EPCnL, CCAPnL}` against `(0, CL)`.

```
   (0,CL) <  {EPCnL,CCAPnL}   ->  output LOW
   (0,CL) >= {EPCnL,CCAPnL}   ->  output HIGH
```

⚠ **Read that again before implementing it: a LARGER compare value means a LONGER low time.**
The duty cycle as a fraction *high* is `(256 − {EPCnL,CCAPnL}) / 256`. A naive model that treats
`CCAPnL` as "duty" gets every brightness inverted, and it will look plausible.

The 9th bit is what buys the endpoints: `{0,0x00}` is permanently high, and `{1,0x00}` = 256 is
permanently low, which an 8-bit compare could not express.

**Double buffering:** when `CL` overflows `0xFF → 0x00`, `{EPCnH, CCAPnH}` is reloaded into
`{EPCnL, CCAPnL}`. So software writes the *next* duty to `CCAPnH`/`EPCnH` and it takes effect at
the next period boundary — "that allows updating the PWM without glitches". **A model that
applies `CCAPnH` immediately will not glitch where real hardware does not, but it will also let
a test pass that should have caught a mid-period write.** Model the reload.

### 5.4 Pins, and a conflict the circuit designer must know about

On the STC12C5A60S2: module 0 is `CCP0/PCA0/PWM0` on **P1.3**, module 1 is `CCP1/PCA1/PWM1` on
**P1.4**, and `ECI` is on **P1.2**. Setting `AUXR1.PCA_P4` moves them to **P4.2**, **P4.3** and
**P4.1**. (Other STC12 variants differ — the STC12C5201AD puts module 0 on P3.7 and module 1 on
P3.5 — so a model must not hardcode this across the family.)

**P1.2, P1.3 and P1.4 are also ADC channels 2, 3 and 4.** So on the default pin mapping you
cannot have PWM on module 0 and analog input on ADC3 at once. `03-potentiometer` already uses
P1.2 for the pot, which is `ECI`. The front end should reject the overlap rather than emit code
that silently loses one of the two.

### 5.5 The other three modes, in brief

`CAPPn`/`CAPNn` enable positive/negative edge capture (both set = either edge); `MATn` makes a
match set `CCFn`; `TOGn` makes a match toggle the pin (high-speed output). 16-bit software timer
is `ECOMn`+`MATn` with software reloading `CCAPnH:CCAPnL`. These matter for *foreign* firmware —
the corpus — rather than for anything this toolchain emits.

### 5.6 What a conforming model must reproduce

1. The counter counts at the selected source and `CF` sets on overflow, software-cleared.
2. PWM output follows the 9-bit compare **in the direction above**, and `{EPCnH,CCAPnH}` reloads
   into `{EPCnL,CCAPnL}` on `CL` wrap, not immediately.
3. `{EPCnL,CCAPnL}` = 0 gives a permanently high pin; = 0x100 gives a permanently low one.
4. Both modules share a frequency and vary independently in duty.
5. For the board layer: a PWM pin is a pin toggling at that duty, and LED brightness is average
   current integrated over ~20 ms — which is what makes `ledBrightness` in boundary B testable
   at last.

## 6. Reset and pins — where generic 8051 lore is wrong

- **Reset is ACTIVE HIGH.** Below 12 MHz a plain 1 kΩ to GND is the entire circuit. The
  10 kΩ + 10 µF network from old 8051 schematics is for active-**low** parts; do not copy it.
- **There is no `EA` pin.** STC removed it; pin 31 is `EX_LVD/RST2/P4.6`. Likewise **no
  PSEN**. Anyone arriving from an AT89C51 expects otherwise, and so do most tutorials.
- PDIP-40: VCC 40, GND 20, RST 9, `P3.0/RxD` 10, `P3.1/TxD` 11, XTAL2 18, XTAL1 19. **P0 runs
  descending**: pin 32 is P0.7, pin 39 is P0.0.
- 5 V part (3.5–5.5 V). The `STC12LE…` sibling is 2.1–3.6 V and 5 V destroys it.

## 7. What a conforming implementation must reproduce

The acceptance ladder, in increasing order of value. Report honestly which rungs you have
actually climbed.

1. Loads an Intel HEX image and executes it.
2. `PxM1`/`PxM0` writes change pin behaviour; push-pull vs quasi-bidirectional is observable.
3. **Timer 0 is exact**: with `FOSC = 11059200` and `T0_RELOAD` above, the tick is 1.000 ms
   with `AUXR.7 = 0` — and demonstrably ~12× faster with `AUXR.7 = 1`.
4. The ADC sequence in §4 completes, sets and requires software-clearing of `ADC_FLAG`, and
   returns a 10-bit value that tracks the input voltage linearly.
5. The PCA block per §5.
6. **Differential execution**: real images produce matching pin/SFR traces. Test images:
   `src/01-blink` and `src/02-adc` in this repo, plus whatever `sb3-creator`'s `generateC()`
   emits (build locally, or `POST` the C to `https://stc-compiler.vercel.app/compile` with
   `{"language":"c","target":"stc12c5a60s2"}`).

## 8. Deliberately out of scope for now

UART1/UART2 and the BRT (baud-rate timer), EEPROM/IAP, the watchdog (`WDT_CONTR` — note it
sits at a *different address* on the STC89, which is why the Keil translator refuses to guess
it), SPI, Timer 1 beyond the standard 8051 behaviour, power-down/idle modes, and the LVD.

Add them when something needs them, and extend this document **first**.
