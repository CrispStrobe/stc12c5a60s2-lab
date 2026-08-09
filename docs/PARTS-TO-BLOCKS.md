# Parts to blocks — what appears in the palette when you draw something

Three things now describe the same hardware and must not drift: the **circuit designer**, the
**block palette**, and the **generated code**. This document fixes how a part becomes a block.

It exists because the mechanism is half-built and the half that is missing is not obvious.
`project.stc.pins` **already** feeds the block dropdowns — `stc12_setpin`, `stc12_toggle` and
`stc12_read` are name-parameterised, so `turn on [led1 ▾]` gets its menu from the declarations
(`sb3-creator/reference/c-target.md`). What is missing is the other direction and the newer parts.

## The rule

> **`project.stc` is the single source of truth, and both sides write it.**

Drawing a part in the designer *adds a declaration*. Adding a declaration *populates the
palette*. There is no second list, no synchronisation step, and no "which one wins" — the same
`{device, clock, pins, ports, parts}` that rides in `project.json` and survives
pseudocode ⇄ blocks ⇄ C.

That is why boundary C is `inferNetlist(declarations) → parts`: the netlist is *derived*. The new
direction is that dropping a part on the canvas writes the declaration the netlist would have
been inferred from — which keeps one source of truth rather than creating a second.

**A part the user drew but never named cannot produce a block.** The name is the block's label,
so the designer must ask for one at drop time, or generate one (`led1`, `led2`) the user can
rename. This is not a detail: an unnamed part is invisible to the palette and looks like a bug.

## What each part contributes

Every part contributes up to three things: a **reporter** (what is it doing), **statements**
(change what it is doing), and an **event hat** (react when it changes). Not every part has all
three, and inventing ones it cannot have is worse than leaving the row short.

| declaration | reporter | statements | event hat |
|---|---|---|---|
| `PIN x OUTPUT` | — | `turn on x` · `turn off x` · `toggle x` · `set x high/low` | — |
| `PIN x INPUT` | `(x)` — pressed/released, polarity-aware | — | **`when x pressed`** · **`when x released`** |
| `PIN x ANALOG` | `(x)` — 0…1023 | — | ⚠ `when x above <n>` — see below |
| `PIN x PWM` | — | `set x to <n> percent` | — |
| `PIN x TONE` | — | `set x to <n> hz` · `turn off x` | — |
| `PORT p OUTPUT` | — | `set p to <n>` | — |
| `PORT p INPUT` | `(p)` — 0…255 | — | — |
| `PART s = 74HC595` | — | `set s to <n>` | — |
| *(program-wide)* | — | `print "…"` · `print <n>` | — |

Existing blocks keep their opcodes. The new rows need new ones, and the naming should follow the
existing convention (`stc12_setpwm`, `stc12_settone`, `stc12_setport`, `stc12_setpart`,
`stc12_print`, `stc12_whenpin`).

## Event hats are the interesting part

`when x pressed` is the block a Scratch user reaches for first, and it does not exist yet. The
ROADMAP's first-cut vocabulary listed it as `when [P3.2] goes [low ▾]`, lowering to `INT0`.

**Prefer polling in the cooperative scheduler over an external interrupt**, for three reasons:

1. **There are two external interrupt pins.** `INT0`/`INT1` are P3.2/P3.3, so an interrupt-based
   hat works for at most two buttons and then silently stops being available. A palette entry
   that works twice is worse than one that always works.
2. **It composes with what already exists.** Each `WHEN` block is already a state machine over a
   millisecond tick that yields at every wait and loop back-edge. An event hat is one more task
   whose first state is "has the pin changed since last tick", which is the same shape and needs
   no new scheduling concept.
3. **Debouncing has to happen anyway.** A mechanical button bounces for milliseconds; an
   interrupt fires on every bounce. The tick is already a debounce interval.

So `when x pressed` lowers to a task that samples `x` each tick and runs its body on a
low-to-high transition of the *logical* value (polarity-aware, as everything else is). Edge, not
level — a held button must not re-run the body every millisecond.

⚠ `when x above <n>` for an ANALOG pin is listed for completeness but should **not** be built
yet: an ADC read is not free, and a hat that quietly polls the ADC at 1 kHz is a performance
trap disguised as a block. Decide the sampling rate deliberately before offering it.

## What this does not change

The generated C. Every row above already has a lowering in `stc-compiler`'s dialect — that is
precisely why the table can be written down now: blocks are a *surface* over the pseudocode, and
`.bw` files remain the reference implementation and the test oracle. If a proposed block has no
sentence in the dialect, that is a signal to add the sentence first, not to special-case the
block.

## Two extensions, not one

These blocks do not all belong in the same palette, because they are not about the same thing.
Scratch extensions are how that distinction is normally drawn, and there should be two:

### `stc12` / `stc12live` — the chip and what is wired to it

Everything in the table above. Pins, ports, parts, tone, PWM, print. It is *about the program*:
every block has a lowering to a line of C, and the same palette serves both the compiled and the
tethered mode (`ROADMAP.md`'s modes A and B), which is exactly why the vocabulary was chosen so
that each block has an obvious form in both.

### `circuit` — the board, and the instruments on it

This one does not exist and its contents are already specified: it is **boundary B of
`simulation-contract.md`** exposed as blocks, and nothing more.

| block | boundary B call | note |
|---|---|---|
| `(voltage at <net>)` | `nodeVoltage` | volts |
| `(current through <part>)` | `branchCurrent` | amperes; forces the real solver |
| `(resistance between <a> and <b>)` | `resistance` | **returns "turn the power off first" when powered** |
| `(brightness of <led>)` | `ledBrightness` | 0…1, current × PWM duty over ~20 ms |
| `(tone of <buzzer>)` | `buzzerTone` | derived from the toggle period, not driven |
| `set <control> to <n>` | `setControl` | turn a knob, press a button |
| `turn power <on/off>` | `setPower` | |

The `resistance` row is the one worth building first even though it is the least useful, because
it is the only block in either extension that **teaches by refusing**: a real multimeter reads
ohms with the power off, and a block that cheerfully reports a number on a live circuit teaches
the wrong physics. The contract already encodes that as a return type rather than a comment
(`number | 'requires-power-off'`); as a block it should say so out loud.

### Meter blocks must sample at display rate, not per edge

**Measured, not assumed** (`bw-board` `f2d8f15`, measured on commit `c4d8031`; supersedes the
earlier `88ac0d6` round, which predates a 68× `advanceTo` optimisation). The numbers:

| path | rate |
|---|---|
| `advanceTo` alone — steady state, nothing changed | 233 K ops/sec |
| `advanceTo` + `setPin` — the PWM loop | 194 K calls/sec |
| `setPin` alone — closed-form solver | 184 K ops/sec |
| `branchCurrent` / `resistance` — the MNA solver | 12 K ops/sec |
| a PWM pin at `CMOD=0x00` | **7.2 K edges/sec** |

So a PWM pin on its own costs nothing: `setPin` never reaches the MNA solver, and a second of
PWM simulates in 75 ms — 13.4× real time, with the brightness correct at 0.0725 for 50% duty.

**But `(current through <led>)` on a PWM'd LED calls `branchCurrent` per edge**, and the number
that decides the design is the *whole* per-edge path, not the MNA solver in isolation. Measured
end to end — `advanceTo` + `setPin` + `branchCurrent` — it sustains **8.0 K edges/sec against
7.2 K: 1.1× real time** (`bw-board` `f2d8f15`; an earlier run said 6.6 K / 0.92×, which was JIT
pessimism from too short a warmup). Two blocks a user would naturally combine, and 1.1× is not
headroom — any emulator speedup erases it, and running faster than real time is gone already.

Quote that figure and not `12 K / 7.2 K = 1.6×`: dividing an isolated operation by a combined
workload flatters it, and the loop has to do the `setPin` and the `advanceTo` that produced the
edge as well.

The fix is in the block, not in the interface: **a meter reporter samples at display rate
(~60 Hz), not once per edge — and that cache is load-bearing, not an optimisation.** It is also
what a real instrument does: a multimeter does not report 7 200 readings a second, it integrates
and shows you one. The block should be honest about that: it reports a *measurement*, not an
instantaneous value.

Do not solve this by batching in boundary A. The board is passive and the MCU owns time
(`simulation-contract.md` boundary A decision 4); making the board coalesce edges would put
scheduling on the wrong side of the line, and the only caller that needs it is the meter.

### The asymmetry that has to be stated

The `circuit` extension is **mostly simulation-only**, and that is not a defect to be hidden:

- `nodeVoltage`, `branchCurrent`, `resistance`, `ledBrightness` have **no equivalent on real
  hardware** unless a meter is attached. On a live board they must be unavailable, not wrong.
- `setControl` is meaningless on a real board — you press the actual button.
- `setPower` may exist on real hardware if the adapter switches VCC (the DTR trick in
  README §2.3), and otherwise does not.

So the `circuit` extension needs the same treatment the debug monitor already got: **declare what
is available on this target and grey out the rest, with the reason.** A greyed block saying
"needs the simulator" is honest; a block that silently returns 0 on real hardware is the thing
this project keeps refusing to ship.

## Where each piece lives

- **The designer writes declarations** — `bw-circuit-ui`, when a part is dropped or renamed.
- **The palette reads them** — `sb3-creator`'s block layer, extending the existing
  name-parameterised menus.
- **The lowering already exists** — `stc-compiler`, except the event hat, which needs the polled
  task described above.
