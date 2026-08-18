# Native micro:bit — the fourth boundary-D implementation

**Why this file exists.** The owner's goal (2026-08-18): micro:bit as a
*first-class* target — an actual compiler, a simulator, and a debugger — wired
into the same run-control surface the 8051 already uses, not a bolt-on. This
document scopes that against the contract that already governs the 8051:
[`DEBUG-CONTROL-MODEL.md`](DEBUG-CONTROL-MODEL.md) — **boundary D**, one
interface, N implementations. micro:bit becomes the **fourth** `DebugTarget`.

It follows that document's own rule: write the contract before building it,
because `step` is a word that a MicroPython VM and an 8051 core implement two
different ways, and both will pass their own tests.

## 0. Licensing — verified, all MIT (2026-08-18)

Every piece we would fork or bundle is permissive. Checked via the GitHub
license API, not assumed (the discipline earned after two wrong licensing
calls this week):

| component | repo | licence | role |
|---|---|---|---|
| **WASM MicroPython simulator** | `microbit-foundation/micropython-microbit-v2-simulator` | **MIT** | the sim we ALREADY ship (`static/microbit-sim/build/firmware.wasm` + `simulator.js`) |
| **MicroPython for micro:bit v2** | `microbit-foundation/micropython-microbit-v2` | **MIT** | the compiler/runtime; the sim is built from it |
| **CODAL runtime** | `lancaster-university/codal` | **MIT** | the C++ device layer (if we ever compile C++ → hex) |
| **reference editor (sim + debugger)** | `microbit-foundation/python-editor-v3` | **MIT** | prior art for a source-line debugger over this sim |

The one non-permissive thing near here — the **Scratch stack** — is (a) pinned
in lite at the **last BSD-3 commit** (`7a72429477eb`, two days before the
2024-11-25 AGPL relicense) and (b) **irrelevant to this vision**: the Scratch
micro:bit `.hex` is a BLE-control firmware for a Scratch-tethered board, not a
compiler/simulator/debugger. It stays out of scope. **Green light to fork.**

### The "micro:bit more" family — a licence split that dictates strategy

The richest prior art for a *fuller* micro:bit extension (all pins, all sensors,
Web Bluetooth without Scratch Link) is the "micro:bit more" family. Its two
halves are licensed differently, verified 2026-08-18:

| repo | role | licence | how we may use it |
|---|---|---|---|
| `microbit-more/mbit-more-v2` | the **Scratch-side extension** (blocks, BLE client, reporters) | **MIT** (© 2020–2022 Koji Yokokawa) | **readable + adaptable with attribution** — a legitimate reference/base for our extension |
| `microbit-more/pxt-mbit-more-v2` | the **micro:bit firmware** (PXT/MakeCode) | **NONE — all rights reserved** | **clean-room concepts ONLY** — study the behaviour/protocol shape, implement from our own understanding, record provenance (the ledcube §7 discipline) |

So the strategy splits cleanly: the extension logic is MIT and reusable; the
firmware side is off-limits for copying and must be re-derived clean-room if we
ever need a custom firmware protocol. **Provenance must be recorded per side** in
whatever we ship — what came from the MIT extension (attributed) vs what was
re-derived from observing the unlicensed firmware.

## 1. What already exists (we are closer than it looks)

Three of the pieces are built and MIT; the tab bug is what hides them.

- **Compiler (real, ours):** `overlay/scratch-gui/src/lib/sb3-creator-micropython.js`
  (434 lines) transpiles the BrickWright dialect → MicroPython
  (`from microbit import *`, `Pin`, `display`, `sleep`).
- **Simulator (real, self-hosted):** `static/microbit-sim/build/` ships
  `firmware.wasm` (1.1 MB, MicroPython-on-WASM) + `simulator.js`, driven by
  `microbit-sim-pane.jsx` over a postMessage bridge. It runs the generated code
  in-browser, no hardware.
- **Flasher toolchain (present):** `dapjs` (Arm DAPLink WebUSB) +
  `@microbit/microbit-universal-hex` are installed; only the firmware `.hex`
  URL is stubbed (`microbit-hex-url.cjs = ''`), for a mundane reason —
  `integrate.mjs` skips the flaky download under `--ignore-scripts`.

**The debug internals are already in the shipped sim.** `simulator.js` contains
`step` / `steps` / `stepEndPosition` / `getState` / `register` / `resume`. What
is missing is the *bridge*: the postMessage protocol the sim-pane speaks today
is only `flash` / `ready` / `request_flash` / `reset` / `serial_output` /
`state_change` / `stop`. There is **no debug channel** — no halt, step,
breakpoint, or register read crosses the iframe boundary. Surfacing those is the
core of the debugger work, not writing a debugger from scratch.

## 1a. Our own micro:bit gallery extension — beyond legacy

The stock Scratch micro:bit extension (`scratch3_microbit`, BSD-3, bundled) is
deliberately thin: display, buttons, a little accelerometer, one pin group. The
owner's requirement is a **first-class extension in the BrickWright gallery with
many more features** — the "micro:bit more" feature envelope and past it. We
build our OWN (not vendor mbit-more wholesale), taking the MIT `mbit-more-v2` as
an attributed reference and the unlicensed `pxt-mbit-more-v2` as clean-room
concepts only. The feature axes to cover, each a block group:

- **Pins — the big gap.** Every pin: digital read/write, **analog in** (0–3.3 V
  → 0–1023), **PWM / analog out**, touch, and pin-mode selection. The legacy
  extension exposes almost none of this.
- **Sensors — all of them:** accelerometer (x/y/z + pitch/roll + gesture),
  magnetometer/compass (heading + raw field), light level, temperature, sound
  level (v2 mic), and the logo touch (v2).
- **Actuators:** tone/music out, servo (angle + continuous), and the 5×5 display
  as a first-class **grid field** — the same `FieldLed8x8`-style painter we built
  for the A2 matrix, resized to 5×5, so the display block is a paint surface not
  a text box.
- **Comms:** radio (send/receive numbers, strings, name/value pairs; group + power),
  and serial/UART. I2C and SPI as expert blocks where the runtime allows.
- **Events:** `WHEN button/pin/gesture/sound …` hats — the edge-triggered model,
  the same shape as the A2 keypad `WHEN key N pressed` hats.

Two structural choices, both from the "more" family's design:

1. **Web Bluetooth directly, no Scratch Link.** `mbit-more-v2` connects over the
   browser's Web Bluetooth API — no native helper app. That is the right model
   for a browser-first tool; it needs a companion micro:bit firmware that speaks
   our BLE service. Whether we re-derive that firmware clean-room (from the
   unlicensed pxt side) or build on the MIT MicroPython radio path is a Stage-4
   decision, recorded when we make it.
2. **Sensor streaming, not per-block polling.** The "more" design streams sensor
   state continuously over a BLE characteristic and answers reporters from a
   local mirror, rather than a round trip per block read. That concept (not its
   code) is what keeps a `forever: point in direction (compass heading)` loop
   from saturating the link — a structure our extension must reproduce.

This extension is the **connectivity + I/O surface**; §2–§6 below are the
**execution + debug** surface. They meet at the compiler (§5 Stage 1): the same
dialect program drives both the gallery extension's blocks and the MicroPython
the simulator/debugger runs.

## 2. The capability matrix — micro:bit as a new column

The single most consequential fact in `DEBUG-CONTROL-MODEL.md §1` is that the
targets are **not equally capable, and the differences are forced by what
executes.** micro:bit is a Python-level VM, not an 8051 core, so its column is
shaped differently again — and `capabilities()` (§7 of that doc) is exactly the
mechanism that lets one front end serve it.

| capability | emu8051-stc | on-chip monitor | **micro:bit sim (target)** |
|---|---|---|---|
| halt / resume | exact, any time | only at a yield | **exact — the VM has `resume`; the bridge must expose halt** |
| step one instruction (`insn`) | free | intrusive | **n/a — MicroPython bytecode, not machine insns; report `insn: false`** |
| step one source line (`line`) | needs line table | no | **native — the sim has `stepEndPosition` over Python source** |
| step one block (`block`, yield→yield) | yes | yes | **n/a — real Python has no cooperative yield machine; see §3** |
| breakpoint at a code address | free | no | **n/a in the 8051 sense** |
| breakpoint at a **source line** | via line table | no | **yes — the sim can trap by Python line (python-editor-v3 does)** |
| data watch (write/read) | yes | polled only | **TBD — depends on what the fork exposes over globals/locals** |
| read registers / memory while halted | all 8051 spaces | curated | **Python frame: locals, globals, call stack; ARM regs optional (§4)** |
| program time freezes while halted | inherently | measured | **inherently — the sim owns its clock, like the emulator** |
| physical world freezes while halted | n/a | never | **n/a (sim) / never (real board, Stage 4)** |

The lesson `capabilities()` already encodes carries straight over: the
debug-panel must **branch on the column**, greying out `insn`/`block` for the
micro:bit target and lighting `line` + source-line breakpoints. A target refuses
by returning a *reason*, never by silently degrading.

## 3. Position — the 8051 model does NOT transfer, and that is the crux

`DEBUG-CONTROL-MODEL.md §2` builds position from the cooperative state machine
`stc_pseudocode.py` emits: `(task, <task>_state)` read out of RAM, yield-to-yield
granularity, free on every target. **micro:bit MicroPython has none of this.** It
runs real Python with a real call stack and a real program counter into the
bytecode; there is no `<task>_state` selector to read.

So the position model **generalises** rather than transfers. Boundary D's
`HaltReason.tasks` (an array of `{task, state, until}`) is an 8051-ism. The
micro:bit target reports position as a **Python frame stack**: `(filename, line,
function, locals)` per frame, top of stack first. This is a genuine extension to
the interface, and the honest way to make it is to widen `HaltReason.position`
to a tagged union — `{kind: 'yield-tasks', tasks: [...]}` for the 8051 cores,
`{kind: 'py-frames', frames: [...]}` for micro:bit — rather than pretend a
Python line is a `<task>_state`. **Pointing a Scratch-block highlighter at a
confidently wrong position is worse than pointing at nothing** (the same
principle `stc_symtab.py` enforces). The `(task, state) → source block` mapping
the 8051 side needs has a micro:bit analogue: `(py file, line) → source block`,
produced by the MicroPython emitter's own `@bw` line markers, not by the sim.

## 4. Address spaces and registers — a second space model

`§6` names five 8051 spaces (`code/iram/sfr/xram/bit`) because they share numeric
addresses. micro:bit is a Cortex-M4 under MicroPython; its natural "memory" is
the **Python object space** (locals/globals/heap), with the ARM register file and
raw memory as an *optional, expert* space the sim may or may not expose. So
`Space` widens: the micro:bit target reports `spaces: ['py-locals', 'py-globals']`
(and possibly `'arm-regs'`, `'ram'` if the fork surfaces them), and the interface
must stop hard-coding the 8051 space enum. This is the same "name the space or two
implementations will be confidently different" discipline, applied to a target
whose spaces the original author did not enumerate.

## 5. The plan — staged, each stage shippable, licensing already clear

**Stage 0 — unhide it (IN PROGRESS).** Fix the device-dropdown → MicroPython-tab
bug (a separate agent). Nothing below is reachable until the tab appears. This
alone turns micro:bit from "looks stubbed" into a working target with live
simulation + transpilation.

**Stage 1 — Compiler as a first-class target.** Harden `sb3-creator-micropython`
to generateC-parity: the same dialect programs the 8051 compiles, the micro:bit
compiles to MicroPython that (a) runs on the sim and (b) is flashable to real
hardware. Emit the `@bw` line markers §3 needs. Oracle: run identical dialect
programs on both targets and diff observable behaviour.

**Stage 2 — Simulator as a first-class run target.** Wire compile → flash-sim →
run cleanly through the existing sim-pane: display, buttons, serial, radio. This
is largely surfacing what the MIT sim already does; no fork needed yet.

**Stage 3 — Debugger (the real engineering).**

> **CORRECTION — investigated 2026-08-19, an earlier assumption here was wrong.**
> This stage was written believing the WASM sim already had `step` /
> `stepEndPosition` / `getState` / `resume` we could fork into a debug channel.
> **It does not.** Reading the shipped `simulator.js`, `firmware.js` and the
> cloned `micropython-microbit-v2-simulator` source: the `step`/`stepEndPosition`
> hits are the AUDIO synthesizer (`sound-emoji-synthesizer.ts`) and `getState`
> is the board SENSOR state, not a debugger. The WASM glue (`firmware.js`)
> exports only `_mp_js_force_stop` plus HAL functions — **no step, no pause, no
> breakpoint, no register/memory read.** The MicroPython VM in this sim runs free
> or is force-stopped; it exposes no single-stepping. Adding that would mean
> rebuilding MicroPython itself with a debug interface (Emscripten) — not a JS
> fork.
>
> **Second, deeper wall:** the app's `DebugTarget` contract
> (`bw-board/debug-session.js`, `debug-target-factory.js`) is built for
> **emulators that own a program clock** — `runFor(budgetNs)` in *program* time,
> the emulator advancing its own cycles. `emu8051`/`avr8js`/`rp2040js`/`m6502`
> all fit. The MicroPython sim is a **black-box real-time VM in an iframe**: it
> has no program clock to budget and no way to run "N ns then halt." So even
> ignoring stepping, it cannot satisfy the boundary-D contract as written.
>
> **The real path (multi-session, redirected):** a native micro:bit debugger
> means an **nRF52 / Cortex-M4 emulator** (the `rp2040js`/`avr8js` pattern — a JS
> CPU emulator with a program clock and instruction stepping) running the
> COMPILED micro:bit firmware, not the MicroPython black-box. That gives `insn`
> stepping, breakpoints, registers and memory for free, and it slots into the
> existing `DebugTarget` factory exactly like the other cores. Whether such an
> nRF52 emulator exists in JS at usable fidelity is the first thing to establish.
> Until then, micro:bit stays a **run target** (Stage 2: compile → sim → run +
> serial), NOT a debug target — and `capabilities()` says so honestly.
>
> **The other feasible path — instrumentation, and it is CODEABLE now.** The
> 8051 debugger's Level-1 position (`DEBUG-CONTROL-MODEL.md §2`) works by *reading
> state*, not by VM stepping — a `<task>_state` static the scheduler updates. The
> micro:bit has the same lever without any emulator: the compiler emits the
> MicroPython, so a **debug build can instrument it** — a `_bw_pos(n)` marker at
> each block that prints position over the sim's existing serial channel, and a
> cooperative breakpoint check that spins on a value the host sets (the sim
> already relays serial both ways). That yields **block-level position and
> block breakpoints** — Level-1/Level-2 fidelity, not `insn` stepping — over the
> sim we already ship, no VM changes. `capabilities()` reports `block` steps and
> `yield`/block breakpoints, refuses `insn`/`line`, exactly as the on-chip 8051
> monitor refuses what it cannot do. This is the honest first micro:bit debugger
> and it is a `generateMicroPython(debug:true)` + serial-protocol + panel-wiring
> job, not a toolchain rebuild. Prefer it over waiting on an nRF52 emulator.
>
> The original fork-the-sim plan below is kept struck-through-in-spirit for the
> record; it is not the route.

~~Fork `micropython-microbit-v2-simulator` (MIT) to add a debug postMessage
channel exposing its `step`/`getState`/`resume`~~ (there is nothing to expose;
see the correction above). The boundary-D wiring, IF a real emulator lands:
  1. **Generalise boundary D** where it is 8051-shaped: `position` as a tagged
     union (§3), `Space` no longer a fixed 8051 enum (§4), `StepKind` honestly
     reduced to what the VM supports (`line`, `over`, `out`; not `insn`/`block`).
     Extend `DEBUG-CONTROL-MODEL.md` FIRST, per its §9 rule.
  2. Implement the micro:bit `DebugTarget` adapter over the forked bridge.
  3. Point the **existing** debug-panel at it — it already must branch on
     `capabilities()`, so a correctly-answering micro:bit target drives the same
     UI the 8051 does. That is what "native, like the 8051" means concretely:
     the same panel, a different implementation behind the one contract.

**Stage 4 — Real hardware.** Un-stub the flasher (fetch/vendor the MIT MicroPython
`.hex`, not the Scratch BLE one), flash real micro:bits over the already-present
dapjs/WebUSB path, push the `.py` filesystem. Optional far term: on-device debug
via CODAL's GDB stub — the micro:bit analogue of the on-chip monitor, a fifth
implementation of the same contract.

## 6. Acceptance — the differential test, micro:bit flavour

Mirroring `§8`'s ladder, report honestly which rungs are climbed:

1. `capabilities()` answered; `state()` tracks run/halt on the sim target.
2. Level-1 position (§3, py-frames) reported on halt, mapped back to the source
   block via the MicroPython emitter's line markers.
3. `step('line')` × N from reset produces the **same source-line sequence** the
   MicroPython reference (`python-editor-v3`'s debugger) produces on the same
   program — the micro:bit analogue of "same PC sequence", and the one test that
   proves our `step` means what the reference's `step` means.
4. A source-line breakpoint halts at the expected line with the expected locals.
5. The same dialect program run on the 8051 target and the micro:bit target
   produces the **same observable behaviour** (display/serial), proving the two
   compilers agree about the program even though they share no code.

## 6a. The ElecFreaks Arcade Shield — a THIRD execution paradigm

The owner has the ElecFreaks micro:bit Arcade shield in hand (a 160×128 colour
TFT, D-pad, A/B, reset — a handheld game console around a micro:bit V2). Covering
it is a first-class goal. **Licensing is entirely MIT** (verified 2026-08-18):

| component | repo | licence |
|---|---|---|
| MakeCode Arcade engine + editor | `microsoft/pxt-arcade`, `microsoft/pxt`, `microsoft/pxt-common-packages` | **MIT** |
| Arcade-shield-on-micro:bit extension | `microbit-apps/display-shield` (canonical), `thomasjball/arcadeshield` (Thomas Ball / MS Research), `gmh5225/pxt-arcadeshield`, `calliope-edu/gamekit` | **MIT** |

**ElecFreaks ships no proprietary code for this** — their "Retro Programming
Arcade" runs the standard MIT MakeCode Arcade stack plus the MIT display-shield
extension. So there is nothing to clean-room here; it is all adoptable with
attribution.

**But Arcade is a different paradigm from everything in §1–§6.** The MicroPython
path (the WASM sim, `sb3-creator-micropython`) is a text/5×5-pixel model. Arcade
is a **colour-display game engine** — sprites, tilemaps, a 160×128 frame buffer,
a game loop — compiled by PXT to ARM, with its **own self-hostable simulator**
(`arcade.makecode.com`, MIT — the same "vendor the WASM sim" move we already made
for micropython-microbit-v2-simulator). So the arcade shield is a **third
micro:bit execution surface**, alongside the MicroPython sim and real hardware:

- **Run target:** fork/vendor the MIT Arcade simulator as another boundary-D
  target (its own capability column — a game loop, not line-stepping Python).
- **Compiler:** a graphics/game block surface in the dialect (sprite, tilemap,
  the colour screen, controller buttons) that lowers to Arcade — distinct from
  the MicroPython lowering, sharing the front half of the pipeline.
- **Debugger:** Arcade's sim exposes pause/step of the game loop; how much
  boundary-D parity it affords is a study, like the MicroPython sim's.

The licence is clear and the pattern (vendor an MIT WASM sim, add a lowering,
plug a boundary-D target) is the same one this whole document is built on.

### This is not a side quest — it is on the game-engine trajectory

The arcade shield is not a foreign paradigm bolted on; it is the next stop on a
road this app is already travelling. BrickWright already runs 6502, Z80 and a
ZX Spectrum sim/emu; it already has **extended Sound and Graphics editors**; and
it already drives LCDs, TFTs and OLEDs across multiple backends. A 160×128 colour
TFT with sprites and tilemaps is the same class of thing, one notch richer — and
the honest end state the owner names is a **versatile multi-platform game engine
for both vector and bitmap graphics**, not a micro:bit-only feature. The arcade
sim is one target of that engine; the Spectrum, the VDP on the 6502, and a future
TFT part are others. Sequence it after the MicroPython path proves the
boundary-D pattern once, but scope it as **graphics-engine infrastructure**, not
as a micro:bit accessory.

### The tie to the Controller panel (already DESIGNED)

The input half of a game engine is already planned: `ROADMAP.md`'s **Controller
panel** (DESIGNED 2026-08-14, owner's Mindstorms-app reference, lane briefed to
bw-blocks) — freely-placeable widgets (joystick x/y −100..100, D-pad,
momentary/toggle buttons, sliders, dial) in an edit-then-play stage mode, each
binding two ways: program-facing (`controller [joy1] x`, `when [btnA] pressed`)
and world-facing (bound to a part parameter, so it works even for compiled C on
the emulated MCU). A game engine needs exactly this on-screen control surface;
the arcade D-pad/A-B are the same widgets pointed at a running game. **Build the
two together** — the Controller panel is the game engine's input layer, the
arcade/graphics targets are its output layer, and both flow through
`board.setControl` / the boundary-D run surface.

### MIT reference material (adoptable, attribution only)

- **`marceld23/StarPilots`** (MIT) — a complete top-down arcade space-shooter
  *for the ElecFreaks Retro shield* (MakeCode Arcade), bilingual EN/DE. A full
  reference game and a source of arcade patterns for the exact hardware in hand.
- **`aliblol/code-club-missions`** (MIT) — mission-based coding curriculum for
  Year 5/6 on micro:bit + ElecFreaks CuteBot + MakeCode Arcade. A pedagogy /
  mission-structure reference for how the arcade+micro:bit ecosystem is taught.

## 7. Deliberately out of scope (for now)

Compiling the dialect to CODAL **C++** → `.hex` (MicroPython is the compiler path
we have); the gdb remote-serial wire format; on-device debug (Stage 4 far term);
the Scratch BLE-control firmware entirely. Add them when something needs them,
and extend this document **first** — the same rule boundary D lives by.
