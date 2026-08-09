# The parts model — when a driver may exist, and when it may not

[`DIALECT-COVERAGE.md`](DIALECT-COVERAGE.md) measured six of sixteen outside demos as things the
dialect should *never* express: bit-banged protocols whose timing depends on how the compiler
scheduled an increment. The conclusion there was that they argue for a **parts library** — a
`read temperature from <pin>` block over a hand-written driver — rather than for more grammar.

This document is the rule that library runs on. It exists because "add a driver" is the point at
which a project like this quietly acquires a large surface of code that cannot be verified, and
the discipline has to be written down before the first one, not after the third.

## The admission test

> **A driver may enter the library when its correctness depends on the ORDER of edges, not on
> their DURATION.**

That single line does most of the work, and it splits the corpus cleanly.

**Passes.** A 74HC595 shift register. Data is sampled on the rising edge of the shift clock and
the latch transfers on its own rising edge; the part is specified up to tens of megahertz and has
**no minimum clock period that a 12 MHz 8051 could violate**. Clock it as slowly as you like.
The demo's `NOP()`s are conservative padding, not a requirement — which is exactly why the driver
is correct without anyone owning an oscilloscope.

**Fails.** 1-Wire, I²C and IrDA. A DS18B20 reset pulse is *at least* 480 µs and a read slot must
be sampled within 15 µs of the falling edge. Those are hard minimums and maximums, and a driver
that meets them by accident of code generation stops meeting them when the optimiser changes, the
part changes, or the clock changes. From the corpus:

```c
j++;                          /* small delay */
DS18B20_DQ = (byte & 0x01);   /* math also introduces needed delay */
```

That is not a driver, it is a coincidence.

## What a failing part needs before it can be admitted

Not "never" — **not yet, and here is the price.** A duration-dependent driver is admissible only
with a *timebase it controls*, so that the delays are measured rather than counted:

- **A hardware counter, polled.** The PCA counter (`CH`/`CL`) free-runs at a known rate — at
  FOSC/12 and 11.0592 MHz, one count is 1.085 µs — and polling it is immune to whatever the
  compiler did with the surrounding code. This is the sanctioned mechanism.
- **A declared resource cost**, in the same shape the debug monitor already reports
  (`DEBUG-CONTROL-MODEL.md` §7): a part that takes the PCA must say so, because PWM then cannot
  have it.
- **A stated tolerance budget** — which deadline, how much margin, at which clock — so that a
  later reader can tell whether a change broke it.

⚠ None of that is written yet, and no duration-dependent part is in the library. The first one
should not be attempted without hardware to check it against, because the failure mode is a
sensor that returns plausible numbers.

## What a part declaration looks like

A part is not a new statement shape. Where it can be, it reuses one:

```
PART display = 74HC595 DATA P3.4 CLOCK P3.6 LATCH P3.5
set display to font[digit]
```

**A shift register is a port that costs three pins instead of eight.** So it takes the same
`set … to …` a `PORT` takes, and the same active-low polarity, and it produces the same eight
loads at the far end. A user who has outgrown their pins should not have to learn a second
vocabulary to say the same thing.

The pins named in a `PART` are *claimed*: declaring one of them again as a `PIN` is refused, for
the same reason a `PIN` inside a `PORT` is refused — two owners of one register, neither
declaration looking wrong on its own.

## The library so far

| part | protocol | admitted | notes |
|---|---|---|---|
| **74HC595** | shift register, 3 wires | **yes** | order-dependent only; unverified on silicon, but no timing to get wrong |
| DS18B20 | 1-Wire | no | needs the timebase and the budget above |
| AT24C02 | I²C | no | same |
| HD44780 / ST7920 | parallel LCD | no | has enable-pulse minimums |
| IrDA | 38 kHz carrier | no | same, plus a carrier to generate |

⚠ Everything in this repository is unverified on silicon, and a driver is a worse thing to have
unverified than a program is: a program that is wrong looks wrong, and a driver that is wrong
returns numbers.
