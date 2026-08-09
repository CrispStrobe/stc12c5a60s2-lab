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
UART1 can remap off those pins (`docs/PINOUT-STC15.md`) — which is why that part
is the likelier first success.

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
