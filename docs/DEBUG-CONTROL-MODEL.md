# The debug control model — boundary D, one contract, three implementations

**Why this file exists.** [`simulation-contract.md`](../../sb3-creator/reference/simulation-contract.md)
fixes three boundaries: A the pin bus, B parts and probes, C inference from the project. Run
control is orthogonal to all three. It is the surface between a front end and *whatever is
executing* — and three different things will execute:

| implementation | licence | where it may live | repo |
|---|---|---|---|
| emu8051 fork with an STC12 model | **MIT** | bundleable — WASM in the browser | `CrispStrobe/emu8051-stc` |
| ucsim fork with an STC12 model | **GPL-2** (part of SDCC) | CI / developer oracle only. **Never bundled.** | `CrispStrobe/ucsim-stc` |
| an on-chip monitor over UART | ours, MIT | `src/10-live-firmware` — **built, never run on silicon** | this repo |

This document lives here, not beside A/B/C, because one of the three implementers is firmware
in this repo and because it depends on [`STC12-PERIPHERAL-MODEL.md`](STC12-PERIPHERAL-MODEL.md),
which is also here.

**The reason to write it before any of it is built** is that `step` is a word three people will
implement three different ways — one instruction, one C line, one Scratch block — and all three
will pass their own tests. Two models that agree while *running* (which `ucsim-stc` and
`emu8051-stc` now demonstrably do) prove nothing about whether they agree while *stepping*.

## 0. What this is not

**Not a wire format.** Each target is reached over whatever transport suits it: in-process
calls into WASM, ucsim's own socket, a framed UART link to the chip. Boundary D is the
*semantics* those transports carry. `sdcdb`'s protocol and gdb's remote serial protocol sit
*below* this line, not at it.

**Not a source mapper.** Turning a `.cdb` into C lines, and C lines into pseudocode blocks,
belongs to `stc-compiler` / `sb3-creator`. Boundary D consumes the result; it does not produce it.

---

## 1. The capability matrix — read this before designing anything against it

The single most consequential fact here is that **the three targets are not equally capable, and
the differences are not incidental — they are forced by the silicon.** An interface that hides
this produces a front end that lies to the user the moment it is pointed at real hardware.

| capability | emu8051-stc | ucsim-stc | on-chip monitor |
|---|---|---|---|
| halt / resume | exact, any time | exact, any time | **only at a yield point** (or in single-step mode, §4) |
| step one instruction | free | free | intrusive — costs P3.2 and real time, §4 |
| step one C line | yes (needs the line table) | yes | **no** |
| step one block (yield to yield) | yes | yes | **yes — the native granularity** |
| breakpoint at an arbitrary code address | free, unlimited | free, unlimited | **no — not through our toolchain**, §5 |
| breakpoint at a yield point | free | free | free |
| data watchpoint (write / read) | yes | yes | **no** — polled sampling only |
| read registers, IRAM, XRAM, SFR while halted | all | all | most, §6 |
| write the same | all | all | most, with hazards, §6 |
| reset | to a defined state | to a defined state | resets into user code, **not** into ISP |
| **program time freezes while halted** | inherently | inherently | **yes — measured**: the monitor clears `TR0`, §3 |
| **the physical world freezes while halted** | n/a | n/a | **never** |

**The monitor row was a conditional until 2026-08-09; it is now a measurement.** Driving
`LIVE_CMD_HALT` through the serial bridge against the firmware on `emu8051-stc` (`cd0ff75`,
32 assertions): `bw_ms` read 67 before a 500 ms halt, 67 during it, and 168 after resume, with
`TR0 = 0` and `TR1 = 1` throughout the halt. So the monitor really does stop program time by
clearing Timer 0 while wall time keeps running, and the reported skew was **527 ms for a 500 ms
halt** — the 27 ms being the round trips either side. That number is what a front end would show a
user, and it is the first time anyone has seen it.

Note what this does *not* verify, and what the row below it still says: on silicon the PCA, the
UART and every other peripheral keep running while the CPU sits in the monitor. Program time
freezing is a firmware behaviour that can regress; the physical world freezing is not on offer at
all. Testing the emulator rows would prove nothing either way — with no `do_inst` there are no
ticks, so those two are true by construction, which is why the row says *inherently*.

`capabilities()` is part of the interface (§7) and every front end must branch on it. A target
refuses by returning a *reason*, never by silently doing something else — the same idiom
boundary B uses for `resistance(): number | 'requires-power-off'`.

---

## 2. Position: where the program is

A debugger's first question is "where am I". On this toolchain there are two fidelity levels,
and the cheap one is nearly free on **all three targets**.

`stc_pseudocode.py` compiles each `WHEN` block to a cooperative state machine, and that state
machine keeps its position in named C statics:

```c
static volatile unsigned int bw_ms;        /* the millisecond tick        */
static unsigned int <task>_state;          /* which yield this task is at */
static unsigned int <task>_until;          /* wait deadline, if waiting   */
```

`<task>_state` is a `switch` selector; every wait and every loop back-edge is a numbered `case`.
`0xFFFF` means the task ran to the end or was stopped.

**Level 1 — yield fidelity. Free, no codegen change, works on every target.**
Position is the tuple `(task, state)`, obtained by *reading three variables out of RAM*. No
instrumentation, no hooks, no breakpoints. `bw_ms` gives the clock, `<task>_state` the position,
`<task>_until` why a task is waiting and until when. On the emulator these are addresses from the
SDCC map file; on the chip they are the same addresses read over the link.

Granularity is therefore **yield-to-yield**, not per-block: a run of straight-line statements
between two yields does not move `<task>_state`. Say so in the UI rather than implying otherwise.

**Level 2 — block fidelity. Needs a codegen change; costs flash and time on the chip.**
The emitter emits a `BW_TRACE(<block-id>)` hook at each block in debug builds and nothing in
release builds. Exact Scratch-style block highlighting, at a price that is negligible on an
emulator and real on a 60 KB part.

**Ship Level 1 first.** It answers the question the Scratch-shaped front end actually asks, on
hardware, for free.

### The dependency this creates

Level 1 needs the **addresses** of `bw_ms`, `<task>_state` and `<task>_until`, and a stable
`(task, state) → source block` mapping. Both come from the emitter and the SDCC map/`.cdb`.
**That is work for `stc-compiler` / `sb3-creator`, not for either emulator.** Neither emulator
agent should implement a symbol resolver; both should take a symbol table as input.

**Both halves exist as of 2026-08-09.** The addresses were always there; the block mapping is
now `yields[].block` in the same symbol table, carried from an `@bw yield` header that
`sb3-creator`'s `generateC(project, {debug: true})` writes — the emitter is the only thing that
knows which Scratch block a `case` label came from, and the C form has lost it by the time
anything else sees the file. `stc_symtab.py` refuses to merge a map that disagrees with the
`case` labels in the same file, because pointing a front end at a confidently wrong block is
worse than pointing at nothing. The front-end design that consumes this is
`sb3-creator/reference/debugger-ui.md`; it is the UI half of this document and nothing in it
changes the semantics here.

---

## 3. Execution state, and what happens to time

```
     ┌── reset ──> halted <──── breakpoint / step-complete / halt() ────┐
     │               │                                                  │
     │             run()                                                │
     │               v                                                  │
     └───────────  running  ──────────────────────────────────────────> ┘
                     │
                  link-lost ──> detached          (on-chip target only)
```

`stepping` is transient and need not be observable. `detached` exists only for the chip: a
serial link can die, and a front end that models this as `halted` will lie.

**Time while halted is the hardest thing in this document.** On an emulator, halting stops
time — trivially, because time is a counter the emulator owns. On silicon it does not:

- Timer 0 keeps counting, `TF0` keeps setting, and on resume the scheduler sees a huge jump and
  fires every overdue task at once. A `WAIT 1 SECOND` that was halted across for two minutes
  completes instantly and every other task starves in a burst.
- The physical world keeps going regardless: capacitors discharge, motors coast, the user keeps
  turning the pot.

**The rule: the monitor freezes program time while halted.** Clear `TR0` on halt, restore it on
resume, and report the wall duration as `skewNs` in the halt reason. Rationale: the *program's*
view of time then matches the emulator's, which is the only thing that makes the two targets
comparable at all — while `skewNs` keeps the front end honest that the *world* did not freeze.

`haltPolicy` is switchable — `'freeze-timers'` (default) or `'free-running'` for cases where
stopping the timer would itself break what is being debugged. The emulator answers
`'freeze-timers'` and ignores the other.

### 3.1 What the board does while halted — boundary A × boundary D

The circuit simulator sits behind
[`simulation-contract.md`](../../sb3-creator/reference/simulation-contract.md) boundary A, where
**the MCU owns time and the board is passive**: it learns that time passed only when the MCU
calls `advanceTo(tNs)`. Halting therefore needs no new interface at all — a halted MCU stops
calling `advanceTo`, so board time stops with program time, and the whole world freezes
coherently. **That is the rule. Do not add a "pause" call to boundary A.**

Three consequences, none of which falls out on its own:

1. **Do not catch up on resume.** The tempting bug is to resume by calling `advanceTo` with the
   real elapsed wall time. Do not: the board would integrate one enormous `dt` in a single step,
   so RC networks jump, PWM brightness averaging is wrong for a frame, and a buzzer's measured
   frequency is nonsense. Resume continues from where program time stopped. This is the board's
   version of the scheduler firing every overdue task at once, and it has the same fix.
2. **`setControl` stays live while halted.** Turning the pot or pressing the button in the UI is
   user *intent*, not physics — it is the one thing that legitimately changes in a frozen world.
   It takes effect at the MCU's next `readAnalog` / `readPin` after resume. Do not queue it, and
   do not refuse it.
3. **A live target's world does not freeze, and the UI must not pretend otherwise.** When the
   panel is mirroring real hardware rather than driving an emulator, halting stops the program,
   not the capacitors, the motors or the person turning the knob. `skewNs` (§7) is exactly the
   signal for this: zero on an emulator, non-zero on silicon. A panel showing a live board with
   `skewNs > 0` is showing a *snapshot of something that kept moving*, and should say so rather
   than presenting it identically to a genuinely frozen simulation.

---

## 4. Stepping — three operations, not one word

| kind | meaning | emulator | chip |
|---|---|---|---|
| `insn` | one instruction | yes | §4.1 |
| `line` | one C source line, per the line table | yes | no |
| `block` | run to the next yield point | yes | **yes** |
| `over` | as `line`, but do not descend into a call: run until `SP` ≤ the entry value | yes | no |
| `out` | run until `SP` < the entry value | yes | no |

`over` and `out` are defined **in terms of `SP`**, deliberately: it is the only definition both
emulators can implement identically without agreeing on a call graph.

**A tick is not a step.** `step('insn')` advances until a *new instruction begins executing* —
an emulator whose core is driven by a tick function must call it repeatedly until any internal
multi-cycle delay is exhausted, not once. Both readings are defensible from the words "step one
instruction", which is exactly why they have to be written down: two implementations that split
here would report different PCs for the same step count and both would be internally consistent.
(Found by the emu8051-stc implementer, whose `tick()` may or may not retire an instruction
depending on `mTickDelay`; the upstream TUI's `opt_step_instruction` already gets this right.)

### 4.1 Instruction stepping on real silicon

Possible, via the classic level-triggered `INT0` technique: hold P3.2 low with `IT0 = 0`, and
the core's guarantee that one instruction of the interrupted program executes after `RETI`
before the still-asserted interrupt is taken again yields exactly one instruction per round trip.

The price is not small: it consumes P3.2, it needs the ISR to be the highest priority, it
perturbs anything with real-time behaviour, and each step is a UART round trip. **It is a mode
the user opts into, not the default.** A monitor that does not implement it at all is still
conforming — it reports `insn: false` and the front end greys out the button.

---

## 5. Breakpoints

```ts
type Breakpoint =
  | {kind: 'code',  addr: number}                         // code space address
  | {kind: 'yield', task: string, state: number}          // §2 Level 1
  | {kind: 'write', space: Space, addr: number, len: number}
  | {kind: 'read',  space: Space, addr: number, len: number};
```

- **`code` on an emulator** is a comparison in the fetch path: free, unlimited.
- **`code` on the chip is the thing MON51 does and we cannot.** Keil's Monitor-51 needs ~5 KB of
  external code memory plus von-Neumann RAM dual-mapped as code *and* xdata, so it can write a
  trap opcode into code space. **The STC12C5A60S2 has no PSEN pin** — it can address external
  XDATA via `MOVX`, but it can never fetch an instruction from anything but internal flash
  ([peripheral model §6](STC12-PERIPHERAL-MODEL.md)). The dual map is unbuildable. The only
  remaining route would be patching flash through IAP — 512-byte sector granularity, slow and
  endurance-limited even if it worked. **It does not work through our toolchain.** `stcgal` 1.10
  is the only ISP path we have, and its `Stc12Option` class (STC10/11/12) exposes exactly eleven
  option bits: reset pin, low-voltage reset, oscillator stable delay, POR delay, clock gain,
  clock source, four watchdog fields, `eeprom_erase_enabled` (erase-on-download — a different
  thing entirely) and `bsl_pindetect_enabled`. There is no "allow IAP to write the program area"
  among them; `program_eeprom_split` exists only on a later-series option class. **So the chip
  reports `code: false`, and this is settled rather than pending.** Reopen it only if someone
  produces an ISP path that is not `stcgal`.
- **`yield` is the one kind every target supports.** On the chip it is a comparison in the
  dispatch loop, evaluated before a task resumes. On an emulator it resolves through the symbol
  table, and there were two defensible readings: the code address of the `case` label, or a
  write-watch on `<task>_state`. They halt at *different instructions* — the write-watch stops
  on the `MOV` that sets the state, one statement earlier.
  **Settled 2026-08-08, by the two emulator implementers jointly: both use the code address**,
  taken from the symbol table's `yields[].addr`, so they agree by construction rather than by
  coincidence. Recorded here because a resolution that lives only in a coordination file is one
  refactor away from being lost.
- **`write` / `read` are emulator-only.** The chip can poll a variable between yields and report
  a change; that is sampling, not a watchpoint, and it must be labelled as such rather than
  presented as the same feature.

---

## 6. Address spaces and registers

8051 spaces share numeric addresses, so they must be named. This is a classic source of two
implementations being confidently different.

| space | range | notes |
|---|---|---|
| `code` | 0x0000–0xEFFF | 60 KB flash. Read-only on the chip except through IAP. |
| `iram` | 0x00–0xFF | 0x80–0xFF reachable only indirectly |
| `sfr` | 0x80–0xFF | **numerically overlaps `iram` and is a different space** |
| `xram` | 0x0000–0x03FF | 1024 B on-chip auxiliary RAM; more if `AUXR.EXTRAM` selects external |
| `bit` | 0x00–0xFF | bit-addressable space |

Registers: `PC`, `A`, `B`, `DPTR` (and `DPTR1` — this part has two), `SP`, `PSW`, and `R0`–`R7`
of the bank currently selected by `PSW.RS1:RS0`. A conforming target reports the bank explicitly
rather than making the front end derive it.

**The SFR access trap on real silicon:** the 8051 has no indirect SFR addressing. `MOV A,direct`
needs a *literal* operand, so a monitor cannot read "SFR number *n*" from a variable. The
workarounds are a ~1 KB table of `MOV A,<sfr>` / `RET` stubs in flash, or a curated set.
**The monitor exposes the curated set — the SFRs in [peripheral model §2](STC12-PERIPHERAL-MODEL.md) —
and reports the rest as unavailable.** Emulators expose all 256 and must not assume the chip does.

**Write hazards on the chip:** writing `SCON`, `SBUF`, `PCON` or the Timer 1 / BRT baud registers
breaks the link the monitor is speaking over. Those are refused with a reason, not attempted.

---

## 7. The interface

Same house style as boundary A: one direction of control, refusals in the type.

```ts
type Space    = 'code' | 'iram' | 'sfr' | 'xram' | 'bit';
type RunState = 'running' | 'halted' | 'detached';
type StepKind = 'insn' | 'line' | 'block' | 'over' | 'out';

interface Capabilities {
    steps:       StepKind[];                  // which of §4 this target implements
    breakpoints: Array<Breakpoint['kind']>;   // which of §5
    spaces:      Space[];                     // which of §6 are readable
    writable:    Space[];                     // which are writable
    sfrs:        number[] | 'all';            // the curated set, or everything
    haltPolicy:  'freeze-timers' | 'free-running';
    timeFreezes: boolean;                     // does halting stop program time?

    /**
     * Peripherals the debugger itself consumes, so a front end can explain a
     * dead feature rather than merely showing one. Empty on an emulator.
     * `null` means the target predates the field and is not saying.
     */
    consumes:    Resource[] | null;
}

type Resource = 'timer0' | 'timer1' | 'timer2' | 'brt' | 'uart1' | 'pca';

interface HaltReason {
    cause:   'breakpoint' | 'step' | 'user' | 'reset' | 'fault' | 'link-lost';
    pc:      number;
    bp?:     BpHandle;
    /** §2 Level 1 position, for every task the symbol table knows about. */
    tasks?:  Array<{task: string; state: number; until?: number}>;
    /** Program-time nanoseconds since reset. */
    tNs:     bigint;
    /** Wall-clock nanoseconds that passed while halted but not counted. 0 on an emulator. */
    skewNs:  bigint;
}

interface DebugTarget {
    capabilities(): Capabilities;
    state(): RunState;

    run(): void;
    /** On the chip this may not take effect until the next yield point. */
    halt(): void;
    step(kind: StepKind, count?: number): void | {unsupported: string};
    reset(): void;

    setBreakpoint(bp: Breakpoint): BpHandle | {unsupported: string};
    clearBreakpoint(h: BpHandle): void;

    readMem(space: Space, addr: number, len: number): Uint8Array | {unsupported: string};
    writeMem(space: Space, addr: number, data: Uint8Array): void | {refused: string};
    regs(): Regs;
    setReg(name: string, value: number): void | {refused: string};

    /** The ONLY call from target to front end. */
    onHalt(cb: (why: HaltReason) => void): void;
}
```

Four decisions, each of which matters:

1. **Refusal is a return value, never an exception and never a silent no-op.** A front end that
   forgets to check gets an object where it expected data, and fails loudly at the point of the
   mistake.
2. **`capabilities()` is queried, not assumed.** There is no "lowest common denominator" mode —
   the emulator would lose most of its value and the chip would still not fit.
3. **The target never calls the front end except through `onHalt`.** One direction of control,
   as at boundary A, so there is no re-entrancy and no scheduler to agree on.
4. **`skewNs` is mandatory, not optional.** It is zero on an emulator and non-zero on the chip,
   which is exactly the difference the front end must be able to show.
5. **A debugger declares what it costs.** On a 60 KB part the monitor is not free: it takes
   Timer 0, Timer 1, UART1 and a baud source, and a program that wanted one of those does not
   work under it. The case that forced this into the interface: a `TONE` pin is Timer 1 toggling
   a GPIO — no PWM path on this chip can make a pitch
   ([peripheral model §5b](STC12-PERIPHERAL-MODEL.md)) — and the monitor wants Timer 1 as the
   wall clock behind `skewNs`. Without `consumes`, the only symptom would be a buzzer that does
   not sound. The set is part-specific and that is itself worth reporting: an STC12 monitor takes
   the BRT for baud, an STC15 one takes Timer 2.

---

## 8. Acceptance — and the differential test that does not exist yet

`ucsim-stc` and `emu8051-stc` currently agree on free-running traces: blink 49/49, adc 54/54,
scheduler 37/37 identical, timing within 0.6%. **None of that covers run control.** Two models
can agree perfectly while running and halt at different instructions.

The ladder, in increasing order of value. Report honestly which rungs you have climbed.

1. `capabilities()` is answered, and `state()` tracks `run()` / `halt()`.
2. Level 1 position (§2) is reported for every task in the symbol table.
3. `step('insn')` × N from reset produces the **same PC sequence** on both emulators, **with
   interrupts masked** (or up to the first interrupt). See the scoping note below — it is load
   bearing, and without it this rung tests something it cannot deliver.
4. A `code` breakpoint halts **both emulators at the same PC**, with identical `A`, `B`, `DPTR`,
   `SP`, `PSW`, and the same digest of IRAM and XRAM.
5. A `yield` breakpoint on `generateC()` output halts both at the same `(task, state)` **and the
   same `bw_ms`**.
6. Write a variable while halted, resume: both produce the same subsequent trace.
7. On an **interrupt-driven** image, where rung 3 does not apply, stepping produces the same
   sequence of *observable peripheral events* — SFR writes and timer flags — on both emulators.
8. The on-chip monitor answers the same reads as the emulator, for the subset it supports, on the
   same image.

**Rungs 3–7 are an extension of `tests/trace.sh`, not a new harness.** Rung 8 needs the bench.

### Why rung 3 stays PC-based, and what it may not be replaced by

It was proposed (emu8051-stc `spec-updates/002`, 2026-08-08) that rung 3 compare SFR and timer-
flag events rather than PCs, on the grounds that PC agreement is a harness property while
peripheral agreement is the product. **Rejected as a replacement, adopted as an addition —
rung 7 above.**

The reasoning: peripheral-event agreement under free-running execution is *already* established
(blink 49/49, adc 54/54, scheduler 37/37, and 16/16 on the 1T timer image). If rung 3 also
compares peripheral events, it re-measures that and this section tests nothing new — while the
one question boundary D was written to answer, *do the two implementations agree about what
`step` means*, goes unasked. PC is the only observable that answers it. Agreement being
"expected and uninformative when it holds" is the property of a good regression test, not an
argument against having one.

**But the objection is not baseless, and rung 3 as originally written could not be met.** With
interrupts live, *when* an ISR is entered depends on cycle counts, and the two cores agree on
timing to 0.6%, not to 0%. Any accumulated cycle difference eventually lands an interrupt
between different instructions and the PC streams part company — which measures cycle-exactness,
not step semantics. Hence the mask, and hence rung 7 for the images the mask excludes.

The second half of the objection — that the two emulators "sample PC at different moments in the
tick cycle" — is answered by §4's tick-is-not-a-step rule rather than by weakening this rung.

### Where the on-chip target stands

`src/10-live-firmware` implements this document's §2 Level 1 position, §3 time freezing, §4
`block` stepping, §5 `yield` breakpoints and §6 memory access, and answers `capabilities()` with
exactly the on-chip row of §1. It compiles to about 6.6 KB of the 60 KB flash.

What is actually *verified* is the framing layer and the host protocol: `include/live-frame.h`
is deliberately free of SFRs, so `make test` runs the same parser the chip runs, and
`tools/live-monitor.py` implements the wire format independently so the two can be diffed
against each other. That found one real defect — an unescaped framing scheme loses a *second*
frame after any truncation, which is why `live_rx_idle()` exists.

**Nothing above the framing layer has been run on silicon**, and no bench session has happened.
An emulator cannot stand in for one here: neither emulator models UART baud generation — the
UART SFRs are register cells with no behavioral model behind them (`ucsim-stc/PARITY-GAPS.md`:
4 of 13 emitted SFR paths are cells-only, all four are UART). The baud reload value (divisor
3 → 115200 at 11.0592 MHz) is a derivation, not a timed bit on a wire. What is unverified: the
UART bring-up, the BRT/T2 baud clock, and whether halting at a yield point behaves as designed
on a real 1T core. `BENCH-SESSION.md` question 4 ("Does HELLO answer?") is the only test of the
entire baud path, and a garbled reply means baud rather than codec.

## 9. Deliberately out of scope

The gdb remote serial protocol wire format; `sdcdb` integration; flash-patch breakpoints on the
chip, which §5 closes rather than defers; profiling and coverage; anything
about the ISP bootloader beyond noting that the monitor and ISP contend for the same UART on
P3.0/P3.1 and only a cold power-on enters ISP.

Add them when something needs them, and extend this document **first**.
