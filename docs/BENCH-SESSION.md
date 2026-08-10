# The bench session — what to do once a board can be programmed

English-only, like the peripheral models: this is a procedure, not a
getting-started document.

Everything in this repository has been verified under emulation and **nothing
has run on silicon**. Four questions are waiting, they can only be answered with
a board on the desk, and a bench session is expensive to repeat — the point of
this document is that one session should answer all four rather than three.

`BENCH-FLASHING.md` is step 0 and a different question: *can we write a program
to a board at all*, through the browser flashers. This document assumes that
works, or that `make flash` does. If neither does, stop here and go there.

---

## Order, and why this order

Do them in this sequence. Each one either unblocks the next or fails cheaply.

| | what it answers | needs |
|---|---|---|
| 1 | Does flashing work with a real peripheral attached? | pot, LED |
| 2 | The ADC's analog path | pot on P1.3 |
| 3 | The `(select, bit) → (x, y, z)` voxel map, and P0 polarity | a cube |
| 4 | The monitor over a real UART, and ISP pin contention | USB-TTL |

1 and 2 are the same flash. 3 needs the cube kit. 4 is the one most likely to
surprise, so it goes last — a failure there does not cost you the others.

---

## 1 + 2. `02-adc` — the analog path

    make EXAMPLE=02-adc flash

Wire per README §3: pot wiper to **P1.3**, ends to VCC and GND, LED1 as usual.

**What to watch:** the blink rate should track the pot smoothly across its
whole travel. Smooth tracking is the result; it means the register sequence
(already verified — `docs/STC12-PERIPHERAL-MODEL.md` §4, and `ucsim-stc`
`f45ecc6`) is driving a working analog front end.

**What to record**, because it is what the emulator cannot tell you:

- the blink period at both extremes of travel, in ms — two numbers
- whether the tracking is monotonic, or jumps/plateaus anywhere
- whether it is stable when the pot is left alone (drift = a floating input or
  a missing `P1ASF` bit for that pin, though the sequence check says otherwise)

**Suspect first, if it does not work:** the pot is on the wrong pin (P1.3 is
ADC channel 3 — check the *pin*, not the channel number), or `P1ASF` was not
set for it, or the pot's ends are swapped so the wiper never reaches a rail.
A dead-still blink rate at either extreme means the ADC is reading a constant,
not that it is broken.

---

## 3. `probe.c` — the voxel map and the polarity, in one flash

This is the highest-value measurement of the session. Two things are unknown
and one program answers both.

    sdcc --iram-size 256 -o build/ src/20-ledcube/probe.c
    make EXAMPLE=20-ledcube flash

`probe.c` walks all 64 `(select, bit)` pairs one at a time, about 600 ms each,
with a blank gap between, and repeats.

**Fill in the table in `src/20-ledcube/README.md`.** Sixty-four rows. Watch
which voxel lights for each `(select, bit)` and write down its position. This is
tedious and it is the only way: the vendor's animation tables encode the answer
without stating it, because they were authored by looking at a cube.

**Polarity falls out of the same observation, and takes ten seconds.** Watch
what happens at `(FE, 01)`:

- **one LED lights** → `P0` is active-**HIGH**, `1` lights an LED. This is what
  the trace says (`emu8051-stc` Finding #14: `0x00` blank 1560×, `0xFF` data
  414×, zero exceptions in 3,930+ writes), and what all four codebases assume
  (`BW_CUBE_ACTIVE_HIGH = 1`).
- **one LED goes dark while seven light** → active-**LOW**, and four files need
  their flag flipped: `src/20-ledcube/main.c`, `ucsim-stc`'s spec-008,
  `sb3-creator`'s emitted kernel, and `bw-circuit-ui`. They are single-sourced
  for exactly this, so it is four one-line edits and nothing else.

Write down which you saw, not just the conclusion. "One LED lit at (FE,01)" is
a fact; "active-high confirmed" is an inference, and the next reader deserves
the fact.

**Also worth noting while the cube is lit:** does it flicker? The clean-room
driver refreshes at 124 Hz (measured), which should be invisible. Visible
flicker means the dwell is not what the emulator says, which would be the first
disagreement between silicon and the model all day.

---

## 4. `10-live-firmware` — the monitor on a real UART

    make EXAMPLE=10-live-firmware flash
    python3 tools/live-monitor.py --port /dev/cu.usbserial-XXXX

The protocol is verified under emulation against **four independent codecs**
(the firmware's C, `live-monitor.py`'s Python, a hand-built C peer, and
`stc12live.js`), and `live-monitor.py` itself has parsed real firmware replies
through a serial bridge. So the wire format is not the risk. Timing is.

**Do these in order, and stop at the first failure — it tells you which layer:**

1. **Does `HELLO` answer?** If yes, the baud rate is right and the framing
   survives a real UART. That single reply is the session's headline result.
2. **`POS`** — does the position match what the program is doing?
3. **`REGS`**, then **`READ`** of `bw_ms` — does it advance between two reads?
4. **Halt at a yield point.** `bw_ms` should stop while wall time continues;
   under emulation the skew came back as 527 ms for a 500 ms halt.

**The known hazard is the pins.** The monitor's UART is on P3.0/P3.1 — the same
pins as the ISP bootloader. `README.md` §2.3 explains why that matters: ISP
entry needs a **cold power-on**, so the adapter that talks to the monitor is the
adapter that programs the board, and both cannot own the line at once. Expect to
unplug and replug rather than reset. If the STC15 is on the desk instead, its
UART1 can remap off those pins (`docs/PINOUT-STC15.md`, `P_SW1` 0xA2 → P3.6/P3.7
or P1.6/P1.7) — which is why that part is the likelier first success.

**The remap is a bench question and cannot be answered before you get there.**
`emu8051-stc` Finding #15 verified the STC15 monitor's Timer 2 baud path and
reports its resources correctly as T2 rather than BRT — so the firmware side is
ready. But the emulator's instant-TX model does not route UART pins at all, so
setting `P_SW1` changes nothing observable under emulation. Nobody can tell you
in advance whether a remapped UART1 actually frees P3.0/P3.1 on silicon.

So test it explicitly rather than assuming: set the remap, confirm the monitor
still answers on the new pins, and then try to enter ISP on P3.0/P3.1 **without
unplugging the monitor**. If that works, the STC12's most annoying constraint is
gone on the STC15 and the tethered story changes — a board that stays
programmable while a debug link is live. If it does not, say so; it is the sort
of thing a datasheet implies and silicon decides.

**Suspect first:** baud. `FOSC` on an untrimmed internal RC is 11–17 MHz and
drifts with temperature (README §4 "Tuning `FOSC`"), so a monitor built for
11.0592 MHz on a chip running at 12.5 will produce framing errors that look like
protocol bugs. If `HELLO` comes back garbled rather than absent, that is the
baud, not the codec.

---

## What to bring back

The four numbers, and the observations behind them:

- two blink periods at the pot's extremes, and whether tracking was smooth
- sixty-four `(select, bit) → (x, y, z)` rows
- what happened at `(FE, 01)`, described rather than concluded
- whether `HELLO` answered, and if so the halt skew in ms

Then the documents that currently say "unverified on silicon" can say what was
seen instead — `CLAUDE.md`'s layout notes, `src/20-ledcube/README.md`'s empty
table, and `docs/DEBUG-CONTROL-MODEL.md`'s capability matrix. Each names exactly
what it is waiting for, so updating them is transcription rather than judgement.

**If something disagrees with the emulators, that is the most valuable result of
the day and it should be written down before it is explained.** Two independent
emulators agreeing with each other has been the standard of proof here all along,
and it is still not the same as agreeing with a chip.

---

## Pre-registered simulator predictions

Written before any bench measurement. Once a measurement exists, any number
produced afterwards is a number produced knowing the answer, and the
comparison stops being a test.

**Only silicon can discharge a prediction on this list.** Every prediction here
was derived from the datasheet and from our own models, so re-deriving it in an
emulator — even a different emulator, even to four decimal places — checks that
we transcribed consistently, not that the chip behaves this way. That is
category 2b in `EVIDENCE-CATEGORIES.md`, and a 2b result agreeing with a 2b
prediction is a tautology wearing a tolerance.

Concretely: an emulator run reporting "124.1 Hz measured against 124 Hz
predicted, 0.1% error" is a real and useful consistency check, and it removes
nothing from the bench session. The refresh prediction below already says so in
its own text — it *is* an emulator figure. The bench exists precisely to supply
the one source that is independent of every document we have read.

So: record emulator agreement next to a prediction, never in place of it. Tick
nothing off this list until a meter, a scope, a photodiode or a pair of eyes has
seen the real board.

### 1 + 2. `02-adc` — blink period at pot extremes

**Derivation:** The program reads P1.3 (ADC channel 3, 10-bit), scales the
result, and uses it as a delay loop count. With FOSC=11.0592 MHz at 1T:

- **Pot at 0V (wiper to GND):** ADC reads ~0. Minimum delay → fastest blink.
  Prediction: **~100 ms period** (depends on the delay loop constant in the
  source; the loop body is ~4 cycles at 1T).
- **Pot at 5V (wiper to VCC):** ADC reads ~1023. Maximum delay → slowest blink.
  Prediction: **~2000 ms period**.
- **Tracking:** should be **monotonic and approximately linear** across travel.
  A jump or plateau at a specific pot position would indicate a stuck ADC bit
  or a nonlinearity in the pot.

**Tolerance:** ±30% on the absolute periods (the delay constant may differ from
what I assume). The *ratio* between the two extremes is the stronger prediction:
it should be ~20:1. A ratio of 1:1 (constant blink) means the ADC is not
reading, or the pot is not wired, or P1ASF is not set.

**Confidence:** MEDIUM. The delay constant is in the source but I have not
traced the exact loop; the ratio is more reliable than the absolute values.

### 3. `probe.c` — polarity and refresh

**Polarity prediction: ACTIVE-HIGH.** At `(FE, 01)` = select line 0 active,
data bit 0 set:

- **One LED lights** (the voxel at scan line 0, column 0).

This is what all four codebases assume (`BW_CUBE_ACTIVE_HIGH = 1`), and what
the emulator trace confirms: `0x00` = blank (1560 occurrences), `0xFF` = all
data bits on (414 occurrences), zero exceptions in 3,930+ P0 writes.

**Confidence:** HIGH. Four independent implementations agree. But all four
derive from the same vendor animation tables, so a shared misreading of the
hardware is possible. This is the prediction most worth checking.

**Refresh prediction: 124 Hz**, invisible to the eye. Measured under emulation
from the scan timing: 8 scan lines × dwell = ~8.06 ms per full frame. Visible
flicker (>~50 Hz threshold) would indicate the dwell is not what the emulator
says — the first disagreement between silicon and the model.

**Tolerance:** ±10% on refresh rate (112–136 Hz). Below ~70 Hz flicker becomes
visible; that would be a real finding.

**Confidence:** HIGH for invisible flicker. The scan timing comes directly from
the PCA/timer configuration, which is verified by two emulators.

### 4. `10-live-firmware` — halt skew

**Prediction:** A 500 ms halt (`bw_halt(500)`) on real silicon should produce
a measured wall-clock duration of **500 ± 5 ms**.

Under emulation, the skew was **527 ms for a 500 ms halt** (27 ms overhead).
On silicon, the overhead should be SMALLER because:
- No emulator per-instruction overhead
- Timer hardware runs at exact FOSC, not emulated tick-counting
- The 27 ms is dominated by the UART round-trip for the halt/resume protocol

**Tolerance:** 480–550 ms. Anything outside this range indicates:
- <480 ms: timer running fast (wrong FOSC calibration)
- >550 ms: UART latency higher than expected (baud mismatch, OS buffering)
- >1000 ms or no response: baud problem, not protocol (suspect first per §4)

**Confidence:** MEDIUM. The protocol overhead depends on the host-side UART
driver latency, which varies by OS and adapter. The prediction is that it
should be CLOSER to 500 ms than the 527 ms measured under emulation.

**Baud caveat:** The emulator does not model baud rate (documented in
`emu8051-stc/docs/UART-ENTRY-POINTS.md`). If `HELLO` answers at all, the baud
is correct. If it does not, the first suspect is FOSC vs expected baud rate,
not the protocol — an untrimmed internal RC at 12.5 MHz vs the expected
11.0592 MHz produces ~13% baud error, enough for framing errors.

### 5. Brightness / PWM — measurable current

**Prediction:** A 50% duty PCA PWM driving an LED through 1 kΩ at VCC=5V
produces an average current of **1.46 mA**.

Derivation:
- LED on (pin LOW, quasi sink): I = (5V - 2V) / (1000Ω + 25Ω) = 2.926 mA
- LED off (pin HIGH, quasi source): I ≈ 0 (230 µA through 21.7 kΩ, reverse-
  biased for the LED)
- Average at 50% duty: 2.926 / 2 = **1.463 mA**

This corresponds to the board's normalised brightness of 0.07248
(= 1.463 mA / 20 mA rated).

**What to measure:** DC milliamps through the LED using a multimeter in series.
A true-RMS meter is not needed at this frequency (3.6 kHz) — the meter's
internal averaging gives the DC average directly.

**Tolerance:** ±20% (1.17–1.76 mA). A factor-of-two error would indicate
the duty cycle is wrong or the LED forward voltage differs from 2.0V.

**Confidence:** HIGH for the ratio (current proportional to duty), MEDIUM for
the absolute value (depends on actual LED Vf and resistor tolerance).
