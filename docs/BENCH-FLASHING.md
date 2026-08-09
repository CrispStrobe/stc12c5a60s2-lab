# Verifying the browser flashers on silicon

English-only, like the peripheral models: this is a verification contract for
an implementation, not a getting-started document.

<https://crispstrobe.github.io/stc-compiler/> can now write a program to a
board over Web Serial, without a terminal. Three paths exist, for three
families that are entered three entirely different ways:

| board | how it is entered | protocol |
|---|---|---|
| ATmega328P, Uno, Nano | pulse DTR to reset into the bootloader | STK500v1 (optiboot) |
| micro:bit, Raspberry Pi Pico | interrupt the running program | MicroPython raw REPL |
| STC12C5A60S2, 5A16S2 | **cold power-on**, nothing else works | STC ISP |

The micro:bit and the Pico share one path exactly, because at this level they
are the same device: a MicroPython REPL over USB CDC. Everything that differs
between them is in the *program*, which is the code generator's problem and
not the flasher's.

**None of the three has ever programmed a real board.** Every protocol was
developed against a simulator and each is green in CI, which establishes that
the bytes are right and says nothing about whether the wire is. This document
is what a bench session needs to close that, and — more usefully — what to
suspect first for each path, because the suspects are known and specific.

It answers one question: *can a program be written to a board at all.* Everything
that comes **after** a successful flash — the ADC's analog path, the cube's voxel
map and `P0` polarity, the monitor over a real UART — is in
[`BENCH-SESSION.md`](BENCH-SESSION.md), which assumes this one already passed.

Web Serial needs Chrome or Edge and a secure context. The GitHub Pages site is
HTTPS, so it qualifies; a `file://` copy will not.

---

## The general shape

For every path: transpile, flash, and watch the LED. Then, before deciding it
works, **change something and flash again** — a board still running yesterday's
program looks exactly like a board that was just programmed successfully.
Halving the blink interval is the cheapest way to tell them apart.

If a path fails, record the browser console and the status line verbatim. Every
error message in the flasher names what it was waiting for and what it saw
instead; that pair is what makes a fix possible, and paraphrasing it throws the
diagnosis away.

---

## 1. ATmega328P / Arduino Uno / Nano

```
DEVICE ATMEGA328P:
  NAME bench
  CLOCK 16000000
  PIN led = D13 OUTPUT
  WHEN started:
    FOREVER:
      toggle led
      wait 500 ms
```

Transpile → **Compile to .hex** → **Flash** → pick the board's port. Expect
the log to show the image size, one line per 128-byte page, then `done`.

**Suspect first: the DTR pulse.** `pulseReset` drops DTR and RTS, waits 250 ms,
raises both, waits 50 ms. That timing is the one thing a simulator cannot
model, and it varies between a genuine Uno, a CH340 clone and a bare ATmega
with an external adapter.

- *"no bootloader answered"* — the reset is not reaching the chip, or optiboot
  has already handed over. Optiboot listens for about a second after reset.
  Confirm the board works at all with `avrdude -c arduino -p m328p -P <port>`;
  if avrdude succeeds where this fails, the fault is the pulse and not the
  protocol.
- *A Nano with the old bootloader* runs at **57600**, not 115200. The flasher
  now probes: 115200 first, then 57600, and the log says which answered. So
  "nothing at 115200 baud" followed by success is a normal line for an older
  Nano, not a fault. If avrdude needs `-b 57600` and this still fails, the
  fault is the pulse rather than the rate.
- *"verify failed at 0x…"* — the write went out and did not stick. Genuinely
  bad news: a wiring or power problem rather than a protocol one.

---

## 2. micro:bit and Raspberry Pi Pico

```
DEVICE MICROBIT:
  NAME bench
  PIN led = P0 OUTPUT
  PIN buzz = P1 TONE
  WHEN started:
    FOREVER:
      toggle led
      wait 400 ms
```

No compile step — MicroPython is interpreted on the device, so **Flash** is
enabled straight after transpiling. It writes `main.py` and restarts the board.

**Suspect first: whether MicroPython is on the board at all.** This path writes
a file over the REPL; it does not install the runtime. For a micro:bit that is
a one-off from <https://python.microbit.org>; for a Pico, hold BOOTSEL while
plugging it in and drop a MicroPython UF2 on the drive that appears. Neither
is something this page does, and neither is a protocol.

- *"timed out waiting for `raw REPL`"* — either there is no MicroPython, or
  something else holds the port. Close the online editor and any serial monitor
  first; DAPLink exposes one CDC port and it is not shareable.
- *"wrote N bytes but the board has M"* — a REPL chunk was swallowed. The size
  read-back exists precisely to catch this rather than leave a truncated
  `main.py` in place. Worth reporting with the two numbers.

For the Pico specifically, the program is where the two boards diverge and so
is what to check once it runs: the pins are `machine.Pin` objects constructed
at the top, the ADC is scaled (`read_u16() >> 6`) so a reading matches what
every other board reports, and waits use `ticks_diff` rather than `<` because
`ticks_ms()` wraps. A Pico that blinks correctly for twelve days and then
stops would be that last one — which is exactly why it is not written that way.

A second Pico program worth running, because it exercises what a micro:bit
cannot: PWM and a tone at once. `set dim to 60 percent` should visibly dim,
and `set buzz to 440 hz` should sound while it does — they are different
PWM slices, and a board where the tone stops the fade means they collided.

---

## 3. STC12C5A60S2

```
DEVICE STC12C5A60S2:
  NAME bench
  CLOCK 11059200
  PIN led = P1.0 OUTPUT ACTIVE LOW
  WHEN started:
    FOREVER:
      toggle led
      wait 500 ms
```

Transpile → **Compile to .hex** → **Flash**. The page begins pulsing `0x7F` and
asks for a power cycle. **Pull the power and put it back.** A reset button will
not do — the ISP bootloader runs only after a cold power-on, which is the
single fact most STC tutorials get wrong and the thing this repository has
documented from the start.

**Suspect first: the baud switch.** Web Serial cannot retune an open port, so
switching from the 2400-baud handshake to 115200 is a close-and-reopen where
stcgal simply assigns a new `baudrate`. Bytes can be lost across that gap, and
it is the one step in the STC path that is structurally different from the
reference implementation rather than merely untested.

- *"no bootloader greeting"* — the power cycle was not cold enough, or the
  adapter is wired straight through rather than crossed. `make info` from this
  repository is the control: if stcgal sees the chip and the page does not, the
  fault is here.
- *Handshake fine, then failure right after "negotiating baud"* — that is the
  reopen. Falling back to a single rate throughout is the obvious fix, and needs
  a transfer baud the BRT register can express: 691200 / (256 − BRT) at
  11.0592 MHz, so 115200 works and 2400 does not.
- *An STC15 or STC89* is refused by name before anything is connected. Those
  are different ISP protocols, not dialects, and `stcgal` remains the way in.

---

## What "verified" would mean

A path is verified when a **changed** program has been written to a real board
and observed to run, twice, on a cold start. Until then the honest status is
what CI actually establishes: the protocol is right against a simulator, and
for the STC it is byte-identical to what stcgal itself emits.

The STC transcript is committed at `scripts/fixtures/stc12-session.json` in the
compiler repository, with the device simulator that produced it, so any bench
finding can be turned back into a test rather than a patch.
