# Roadmap: an STC12C5A60S2 back-end for BrickWright


This repo is the hardware-and-toolchain groundwork. The destination is a
**BrickWright extension** that lets you drive — and then *permanently program* —
an STC12C5A60S2 from Scratch blocks, using the same compile-and-flash pipeline
documented in the [README](../README.md).

## Where this plugs into the existing stack

BrickWright is already a four-representation compiler
(**pseudocode ⇄ blocks ⇄ Python ⇄ JavaScript**, `sb3-creator/src/utils/sb3Creator.js`)
with hardware extensions layered on top. Two precedents matter here:

| Precedent | Shape | What we borrow |
|---|---|---|
| `legoev3direct`, `legonxt`, `legoboostunified` | **Live/tethered** — blocks send direct commands over a link to firmware already on the brick | The runtime-driver pattern (`RUNTIME_EXTENSIONS` registry) and the transport-adapter split in `overlay/scratch-vm/src/extensions/crispstrobe/adapter.js` |
| `ev3lms`, and the NBC/LMSASM → NXT/EV3 bytecode path in `legacy-lego-compiler` / `brickwright-bridges` | **Compiled** — blocks become source, source becomes machine code, machine code gets uploaded | The "compile server + upload" architecture, and the idea that a target is just another code generator alongside `generatePython` / `generateJavaScript` |

The STC12 wants **both**, for the same reason the EV3 has both.

## Two modes

### Mode A — tethered (`stc12live`)

A small resident firmware sits on the chip and listens on UART1
(`P3.0`/`P3.1`, the same pins ISP uses). Blocks send framed commands; the
firmware executes them and replies. Blocks run *now*, with no reflash, so the
edit loop is as fast as any other Scratch extension.

* Transport: **Web Serial API** (Chrome/Edge desktop), reusing the adapter
  abstraction that already fronts BLE / Bluetooth Classic / WebSocket bridges.
* Cost: the chip is not autonomous — unplug the USB cable and it stops.
* This is the mode to build **first**. It is where the block vocabulary gets
  designed and validated, and it needs no compiler in the browser.

### Mode B — compiled (`stc12`)

Blocks become C, C becomes a `.hex`, the `.hex` gets flashed. The chip then
runs standalone, on a battery, with no computer attached.

```
  Scratch blocks
        │  sb3Creator: existing block walkers
        ▼
  BrickWright IR  ──▶ pseudocode / Python / JavaScript   (already exists)
        │
        │  NEW: generateC() — a fifth code generator
        ▼
  C targeting SDCC + <stc12.h>
        │  NEW: compile service (or SDCC-in-WASM)
        ▼
  .ihx / .hex
        │  NEW: stcgal's STC12 ISP protocol, reimplemented over Web Serial
        ▼
  STC12C5A60S2
```

## Block vocabulary (first cut)

Deliberately small, and chosen so that **every block has an obvious, cheap
lowering to both a live command and a line of C**. This is the same
sink/source and port-mode model documented in
[PINOUT.md](PINOUT.md) — the blocks hide the register pairs, not the physics.

| Block | Live command | Compiles to |
|---|---|---|
| `set pin [P1.0] mode [output ▾]` | `MODE p m` | `P1M1 &= ~m; P1M0 \|= m;` |
| `set pin [P1.0] to [low ▾]` | `WRITE p v` | `P1_0 = 0;` |
| `(read pin [P1.0])` | `READ p` | `P1_0` |
| `wait [1] seconds` | *(host-side)* | `delay_ms(1000);` |
| `(read analog [ADC0])` | `ADC ch` | ADC_CONTR sequence → 10-bit result |
| `set PWM [CCP0] to [50] %` | `PWM ch duty` | PCA module in PWM mode |
| `(millis since start)` | `TICKS` | free-running Timer 1 counter |
| `when [P3.2] goes [low ▾]` | polled by host | `INT0` external interrupt |
| `serial print [hello]` | — | UART1 at a fixed baud |

Non-goals for v1: floating point (SDCC's soft-float on an 8051 is enormous),
recursion beyond shallow depth (248 bytes of stack), and dynamic allocation.

## Why the compiled mode is genuinely harder than the LEGO targets

Worth being honest about, because it shapes the order of work:

1. **No VM to target.** NXT and EV3 both have a bytecode interpreter with
   variables, threads and scheduling already built in. The STC12 has none of
   that — a Scratch "when green flag clicked" script has to become a `main()`
   loop, and two parallel scripts have to become either a cooperative
   round-robin in that loop or a timer ISR. That scheduling decision is the
   central design problem.
2. **Resources are finite and small.** 60 KB flash is generous, but there are
   **248 usable bytes of stack** and 256 B of directly-addressable RAM. Scratch
   variables have to be allocated statically, and the generator has to fail
   loudly rather than silently overflow.
3. **The compiler is native.** SDCC is a C program, not a JS library. Either it
   runs server-side (like `legacy-lego-compiler` already does for NBC and
   LMSASM — this is the well-trodden path) or it gets compiled to WASM.

   **Decided 2026-08-09: WASM, for the 8051 targets.** The server-side path was
   taken first and works, but it brought two problems that have nothing to do
   with compiling. The deploy rate limit on the host's free tier left the
   service ~50 commits stale for a day; and the host's glibc pins it to SDCC
   4.0.0 while this repo builds with 4.5.0, so the two produce different
   firmware — `01-blink` is 888 bytes remotely and 996 locally. WASM has no
   glibc and needs no deploy, so it removes both at once rather than working
   around either. `gbdk-emscripten` already ships this shape for the z80 ports
   at ~1.3 MB of WASM, so it is a port and not an experiment. The build must be
   **single-threaded**: GitHub Pages cannot set the COOP/COEP headers that
   `SharedArrayBuffer` and WASM threads require.

   **AVR: decided 2026-08-10 — drop browser compiling for now, revisit via a
   small hosted service, and port `avr-gcc` to WASM only if someone asks.**
   Measured rather than guessed: the minimum useful `avr-gcc` is 16.1 MB of
   native code (`cc1plus` plus `as`/`ld`/`objcopy`) once `lto1` is dropped and
   the Arduino core is precompiled to `core.a`, which estimates to 7–11 MB
   compressed — roughly 8× the SDCC payload, for an audience that already has a
   toolchain. The governing argument is allocation, not difficulty: this is an
   8051 project and AVR is a bonus target, so the largest engineering item on
   the board should not be aimed at it. Full reasoning, and the correction that
   `avr-gcc`'s GMP/MPFR/MPC dependency is already largely scripted for WASM in
   `math-stack-ios-builder`, are in `stc-compiler`'s README so nobody derives
   them twice.
4. **Flashing needs a power cycle.** The STC bootloader only listens right
   after a cold boot (see README §2.3). In the browser this means either
   instructing the user to unplug VCC, or requiring the DTR-switches-power
   hack so `setSignals({dataTerminalReady})` can do it automatically.

## Suggested order of work

1. ~~**Grow this repo's C primitives** — `02-gpio-input`, `03-pwm`, `04-adc`,
   `05-uart`.~~ — **Built, and cross-checked between two emulators — not
   silicon.** GPIO, ADC, PWM and UART all exist and agree across emu8051 and
   ucsim, which is **category 2b**; `BENCH-ADC`, `BENCH-PWM` and `BENCH-UART`
   are the bench sessions that would raise them.

   > This line said "GPIO and ADC proven on real hardware" until 2026-08-10.
   > It was wrong, and wrong in the direction this project exists to avoid.
   > `VERIFICATION-LEDGER.md` opens with "Nothing here has run on real
   > silicon", records the ADC as *"2b register sequence only — analog path
   > open"*, and leaves `BENCH-ADC` outstanding for exactly the reading that
   > would settle it. `DEBUG-CONTROL-MODEL.md` says plainly that no bench
   > session has happened. The claim was introduced by the commit that set out
   > to *level the docs with evidence*, which is worth recording: the pass that
   > audits claims is itself a place where claims get made.
2. ~~**Write the resident firmware** (`10-live-firmware`) implementing the framed
   command protocol, built from those primitives.~~ — **DONE 2026-08-09, verified
   under emulation.** It builds, boots and answers `HELLO` / `POS` / `REGS` /
   `READ` over the wire, and recovers from a torn frame via the idle timeout.
   Three independent codecs agree on the format: the firmware's own
   (`include/live-frame.h`), `tools/live-monitor.py`'s Python one, and a
   hand-built C peer, with the Python tool's own `Decoder` parsing real firmware
   replies through a serial bridge. Nothing has run on silicon — that is the
   bench session, and it is now the only thing between here and step 3.
3. **Ship `stc12live`** as an extension in `CrispStrobe/extensions`, using Web
   Serial through the existing adapter layer. At this point blocks control real
   hardware.
4. **Reimplement the STC12 ISP protocol in JavaScript**, ported from
   `stcgal/protocols.py` (MIT). Validate it against a known-good `.hex` built
   here — the round-trip is easy to test because `make info` gives you the same
   handshake to compare against.
5. ~~**Add `generateC()`** to `sb3Creator.js`~~ — **DONE 2026-08-08.** It sits
   beside `generatePython` / `generateJavaScript` with `cRep`/`cCond`/
   `cStackBlock` (ports `stmts_c`) and `cTaskBlock` (ports `stmts_task`), and
   ported the scheduling / FOSC-12 / active-low decisions from
   `stc-compiler/stc_pseudocode.py` unchanged. A new block surface carries the
   hardware: `DEVICE` / `CLOCK` / `PIN` declarations plus `turn on/off`,
   `set high/low`, `toggle`, `read` — spelled exactly as this repo's
   `pseudocode/*.bw`, so a `.bw` file *is* a Brickwright program. 37 offline
   checks plus 4 live ones that build every fixture through
   `POST /compile {"language":"c"}`. Write-up: `sb3-creator/reference/c-target.md`;
   vendored into `brickwright` `develop` and `brickwright-lite` `main`, each with
   a read-only **🔌 C (STC12)** tab.
   **C is now TWO-WAY** (`cToPseudocode.js`, 2026-08-08): it reads our own C
   exactly, via an `@bw` marker header the emitter adds for what the flat form
   loses, and reads **hand-written firmware** — including this repo's own
   `src/01-blink/main.c` — by inference: pins from `#define LED1 P1_0` /
   `sbit`, polarity from the `LED_ON 0` idiom, clock from `#define FOSC_HZ`,
   with every inference reported rather than guessed. The register prologue
   moved into `bw_setup()` so setup is distinguishable from program.
   Not inverted: the cooperative-scheduler form (it warns). `keil2sdcc` (C→C)
   now widens this front end's input, so a Keil project can reach blocks by
   passing through both; `stc_disasm` (HEX→asm) remains its own harder track.
   Full reasoning in `sb3-creator/reference/c-target.md`.
6. **Stand up the compile endpoint** in `legacy-lego-compiler` next to the
   existing NBC/LMSASM ones: POST C, get back a `.hex`.
7. **Ship `stc12`** (compiled mode), reusing the flasher from step 4.

## Open questions

* **Scheduling model — DECIDED and prototyped (2026-08-08).** Cooperative
  round-robin, with one twist from Scratch's own contract: a Timer-0 ISR only
  advances a millisecond counter, and every `WHEN` script compiles to a
  Duff's-device state machine that yields at every wait **and at every loop
  back-edge** — so a busy `FOREVER` loop cannot starve the others, exactly as
  in Scratch. No preemption, no per-task stacks, no register-banking
  subtleties; deadlines are wraparound-safe 16-bit compares. Implemented in
  `stc-compiler`'s pseudocode front end: several `WHEN started:` blocks now
  compile and run concurrently (single-`WHEN` programs keep the old
  straight-line emission). One documented limit: with several scripts,
  procedures may not contain waits — the same shape as Scratch, where custom
  blocks run to completion. `generateC()` in sb3-creator should adopt this
  scheme unchanged; `stc-compiler` is its reference and oracle.
* **Chip families — also settled.** The same pseudocode now emits
  timing-correct code for `STC12C5A60S2`, `STC89C52(RC)` and `STC15F2K60S2`:
  everything is timed off Timer 0 at FOSC/12, which 12T and 1T parts count
  identically, so the drop-in-socket upgrade (STC12 into an STC89 board)
  changes nothing. Software delay loops would have run 6-12x fast — which is
  why the generator never emits one, and why the Keil translator now warns
  when it sees them in migrated code.
* **What the target is, and two things that are NOT concessions
  (2026-08-08).** The bar to clear is 8051 co-simulation in mixed-mode SPICE
  with source-level debugging — that combination exists in commercial tooling
  and is what "good" looks like here. Two constraints follow from licences
  rather than from features, and both are recorded in the licence audit below:
  an AGPLv3 implementation can be learned from but never linked, and the
  MIT-licensed browser UI elements can be reused directly. No existing
  browser-based 8051 core was found to build on — the ones surveyed model
  peripherals rather than cores — which is why the core has to be written.
  What is genuinely unoccupied is **Scratch blocks → bare-metal 8051 with no
  Arduino runtime**: the block-based toolchains surveyed all assume a
  framework. That, and two independently written emulators cross-validated
  against 349 real firmware images.
  **Two corrections to an earlier reading of this comparison.** First, the
  circuit simulator is a *core deliverable*, not something to leave to existing
  desktop tools: it is being built now (`bw-board`, `bw-circuit-ui`), the
  teaching goal needs a real solver rather than a plausible animation, and "we
  will not beat SPICE" is not a reason to stop — it is a reason to be honest
  about fidelity. Second, **peripheral breadth is a goal, not scope creep.** The
  evidence was already in hand and read the wrong way round: **220 of 349**
  third-party firmware images pass differential execution *strictly* — both
  event streams fully identical — which is a *run-foreign-firmware* capability,
  and every peripheral added raises it. The emitter's own needs are a floor on
  the model, never a ceiling.
  ⚠ **That figure is a correction.** It was reported here as 275/349 until
  2026-08-09, when `ucsim-stc` tightened its own metric and found that the
  looser count had been folding in 54 *prefix-only* matches — runs where the
  shorter event stream matched as a prefix but the lengths differed, which
  means one model stopped emitting or the other emitted extra. That is not
  agreement. The argument above survives the correction; the number did not,
  and a metric that flatters itself is worth less than a smaller honest one.
* **The dialect was measured against somebody else's corpus (2026-08-09).**
  [`DIALECT-COVERAGE.md`](DIALECT-COVERAGE.md) scores all sixteen
  [treideme/stc89c52-demos](https://github.com/treideme/stc89c52-demos)
  (Apache-2.0, cited not vendored) and the split is clean: **5 expressible
  today, 5 blocked on exactly two features, 6 that should never be
  expressible.** The two features are **whole-port I/O** (`P0 = pattern`, which
  no number of single-pin statements substitutes for) and **indexed lookup
  tables** (a seven-segment font is a table). Both are forced by the two things
  people build after a blinking LED — a digit display and an LED matrix — so
  10 of 16 are reachable for a small, non-speculative addition. The last six
  bit-bang protocols against microsecond deadlines, with timing that depends on
  how the compiler scheduled an increment; blocks that emitted that would be
  blocks that sometimes work. They are the argument for a **parts library**
  instead — a `read temperature from <pin>` block over a hand-written,
  timing-audited driver — and those six demos are a good specification for its
  first entries. Where we already come out ahead: the corpus's own delays are
  cycle-counted busy loops, which is the construct that breaks 6-12x on a 1T
  part, and everything we emit is Timer 0 at FOSC/12 instead.
* **Example bundles are the integration test (2026-08-08).** `make examples`
  builds one bundle per pseudocode program — source, generated C, `.hex`,
  `pins.json` for boundary C, `symbols.json` for boundary D — committed so
  another repo needs no toolchain to open one. Every layer had its own tests;
  nothing tested that they compose. The set covers all four `inferNetlist` rows
  and the builder fails if that regresses. **The gap it exposes is the next
  work:** everything is GPIO and ADC because that is all the emitter emits, so
  `ledBrightness` and `buzzerTone` — both specified in boundary B — have now
  been exercised. **PCA PWM is built and measured** (category 2b — two models
  correcting each other, anchored by arithmetic the driver fixes in advance):
  - 8-bit PWM: period 277561 ns, duty 33/50/75% at 84/128/192 counts
    (`ucsim-stc` `dafbaf9`)
  - 16-bit compare/match (servo): 1499.6 µs at 90°, 50.0 Hz frame
    (`bw-board` `c02fa9f`)
  - UART TX: 86.8 µs/frame at 115200 (`ucsim-stc` `e426929`)
  - 14 device blocks have real drivers (`ucsim-stc` `a78a8f0`, `bw-board` `57ef1fa`)

  ⚠ **None of this is silicon.** The ADC *register sequence* is verified
  (two models agree) but its *analog path* is not — that is `BENCH-ADC`.
  PWM duty cycle is verified between models but not against a frequency
  counter — that is `BENCH-PWM`. Do not read "verified" without the category.
* **STC15 is a delta; STC8H is a separate project (decided 2026-08-08).**
  [`STC15-PERIPHERAL-MODEL.md`](STC15-PERIPHERAL-MODEL.md) says only what differs
  from the STC12: 74 SFRs identical, **no register keeps its name at a different
  address**, and three traps — `ADRJ` moves from `AUXR1.2` to `CLK_DIV.5`, the
  `AUXR` baud bits become Timer 2 bits, and `WAKE_CLKO` becomes `INT_CLKO`. There
  is STC15 silicon on the bench, so this is the family most likely to produce the
  project's **first verified-on-hardware** peripheral claim — the ADC core
  registers are identical, so confirming that sequence on an STC15 is real
  evidence for the STC12 too.
  **STC8H is deliberately not started.** It is not a variant: 12-bit ADC with
  different control registers, PWMA/PWMB advanced timers instead of a bare PCA, a
  different clock tree, hardware I²C, more ports. It needs its own reference
  manual read, its own model document, its own header — SDCC ships neither
  `stc15.h` nor `stc8h.h`. Two open questions worth answering first: whether
  `stcgal` 1.10's `stc8`/`stc8d`/`stc8g` protocols actually cover STC8H (no
  handler is named for it, and `stc8prog` exists as the alternative), and whether
  `Stc8Option`'s `program_eeprom_split` — a bit with no STC12 equivalent — would
  let IAP write the program area. If it does, **real code breakpoints are possible
  on STC8 where `DEBUG-CONTROL-MODEL.md` §5 closes them as impossible on STC12**,
  which would make STC8H the better debug target.
* **The peripheral model is now written down once, for everyone**:
  [`STC12-PERIPHERAL-MODEL.md`](STC12-PERIPHERAL-MODEL.md) (2026-08-08). A ucsim
  fork (GPL-2, CI oracle only), an emu8051 fork (**MIT**, so bundleable in the
  browser) and the simulator's board layer all need to agree on what this chip
  does; three independent models would mean three different answers. Register
  addresses in it are cross-checked facts; bit layouts still to be read from the
  datasheet are marked. **The ADC section carries the unverified-on-silicon
  warning** — an emulator written from the datasheet can show the sequence is
  self-consistent but cannot confirm it is right.
* **Two surfaces the hardware story still needs (raised 2026-08-08).** Neither
  exists; both are additive to Scratch's stage and need no emitter changes.
  1. **A hardware interaction / visualisation panel** — what
     [S4A](https://s4a.cat/index_en.html) does with its board picture, but
     modern and multi-device: LED states, pin levels, pot readings, motor
     speeds, live beside the stage. One panel, two sources: *simulated* (from
     the emulator or the neutral driver shim) and *live* (mirroring real
     hardware over the tethered link). It rides on sb3-creator's existing
     `RUNTIME_EXTENSIONS` driver contract. Order: LEGO hubs, this board, later
     Arduino.
  2. **A simulator / emulator / debugger view** — run a `.hex` with nothing
     plugged in: step, breakpoints, SFR + register view, memory, pin state.
     `emu8051` is the UX reference (its TUI shows the right panes —
     <https://reidemeister.com/blog/2022.07.03>); `ucsim`/`s51` is the engine
     `stc-compiler` already runs differential execution against. **The known
     gap: no ucsim build ships an STC model** (verified at git head 0.9.9), so
     the actual work is writing one — the SFR set, the ADC, the PCA, the 1T
     timing. That model would also be the cheapest way to close out the ADC
     question below without a bench session. **The run-control surface itself is
     now specified once, as boundary D:**
     [`DEBUG-CONTROL-MODEL.md`](DEBUG-CONTROL-MODEL.md) (2026-08-08) — both
     emulator forks and the future on-chip monitor implement that, not their own
     reading of what `step` means. For the browser, ucsim or emu8051
     compiled to WASM. **Licences checked 2026-08-08: emu8051 is MIT, and so are
     Wokwi's avr8js and wokwi-elements — so the browser path is genuinely open;
     ucsim/QEMU/unicorn are all GPL-2 and stay CI-only.** Full architecture,
     including the circuit-simulation and virtual-multimeter question, in
     `sb3-creator/reference/simulation.md`.
* **Board definition.** Right now `include/board.h` hardcodes one two-LED rig.
  Blocks will need a board descriptor the generator reads — probably JSON,
  probably shared with the live firmware so both modes agree on pin names.
* ~~**SDCC in WASM.** Unknown effort; worth a spike before committing to the
  server path.~~ **Answered 2026-08-09 — doing it.** Removes the server
  dependency entirely and works offline, which matters for the Tauri/iOS/
  Android wrappers. Not a spike after all: `gbdk-emscripten` already ships an
  Emscripten SDCC for the z80 ports at ~1.3 MB of WASM, so this is a port with
  a different `--port` flag, not research. Details and the two constraints
  (single-threaded; pin 4.5.0 rather than the hosted 4.0.0) are in §"Why the
  compiled mode is genuinely harder", point 3.
* **Licensing.** SDCC is GPL, but `mcs51/stc12.h` carries the standard linking
  exception, so generated binaries are unencumbered. `stcgal` is MIT, so a JS
  port is fine for BrickWright-lite's fully-permissive requirement. Neither
  blocks the store-shippable track — but a WASM SDCC bundled into the app
  *would* pull GPL into the bundle, which is exactly the thing `brickwright-lite`
  exists to avoid. Server-side compilation sidesteps that.
