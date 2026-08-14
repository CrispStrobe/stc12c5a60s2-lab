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
    (`ucsim-stc` `1d3c932`)
  - 16-bit compare/match (servo): 1499.6 µs at 90°, 50.0 Hz frame
    (`bw-board` `cce2192`)
  - UART TX: 86.8 µs/frame at 115200 (`ucsim-stc` `644c5c6`)
  - 14 device blocks have real drivers (`ucsim-stc` `498370f`, `bw-board` `62d961b`)

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

## Multi-architecture expansion (assessed 2026-08-10; running)

The 8051 stack was built in layers on purpose, and the layers are the asset:
boundary A (pins ↔ circuit engine), boundary D (run control), and a codegen
that owns the cooperative state-machine contract. New targets slot into those
seams; nothing above them changes.

**Assessment (full matrix in the campaign notes):**

* **AVR — Arduino Nano / Uno (ATmega328P): first.** The emulator problem is
  already solved in the open: `avr8js` is MIT, pure TypeScript, browser-native,
  and from the same family as the `@wokwi/elements` art the designer already
  renders. Run control is *easier* than the 8051 — the emulator is a JS
  stepping loop, RAM is a typed array, and since our codegen emits the
  `<task>_state` variables, the Level-1 position protocol ports unchanged.
  Compilation: hosted `avr-gcc` behind the same REST pattern as the SDCC
  service (small sketches compile in ~1–2 s; GPL toolchain as a *service*, the
  precedent already set). ADC is injectable in `avr8js`, so pot → channel maps
  onto boundary A directly.
* **RP2040 — Pi Pico: second. ROUTE DECIDED 2026-08-12: bare-metal C first,
  MicroPython later as an additive runtime.** The original lean here was
  MicroPython ("no compiler at all"), but the facts changed once the AVR
  chain landed: the boundary-A adapter AND the boundary-D debug target for
  `rp2040js` are built and oracle-tested (bw-board `255dd78`), and both
  speak the same `<task>_state` position protocol as the other cores —
  which MicroPython cannot feed without inventing a second, weaker
  debugging story. Evidence gathered for the C route:
    - **Toolchain fits.** The hosted avr-gcc bundle is 36 MB; our generated
      C is freestanding (no libc — UART/ADC are register writes; only
      libgcc's `__aeabi_uidiv` is needed, Cortex-M0 has no divide). A
      gcc + binutils + v6-m-libgcc bundle, no newlib, lands well inside
      the service's limits via the same fetch/stage pattern as
      `fetch-avr-gcc.sh`.
    - **No bootrom needed for emulation.** Images link at SRAM 0x20000000;
      the adapter loads halfwords and jumps — proven by the hand-assembled
      oracles. Flash/UF2 (boot2 + vector table + elf2uf2) is only needed
      for real silicon and comes later.
    - **Timebase: the RP2040 TIMER, ISR-free.** `rp2040js` implements the
      1 MHz microsecond counter (TIMELR) and SysTick both; `bw_now()`
      reads TIMELR/1000 directly — no tick ISR at all, simpler than
      either sibling core and race-free by construction.
    - **Symbols: same pattern as AVR.** arm-none-eabi-objdump -t +
      --dwarf=decodedline through the avr_symtab machinery (ARM addresses
      are absolute; no 0x800000 strip, no word/byte trap).
  The MicroPython route (official UF2 into flash, generatePython feeds a
  REPL, capabilities honestly reduced — no code breakpoints, no insn
  stepping) stays on the map as a *second runtime* for the same board,
  and the capability matrix already knows how to say so.
* **AVR family widening — ADJUDICATED 2026-08-13, the cheap wins:** avr8js
  ships port configs A through L (the ATmega2560's full set) and an ATtiny
  timer config — Mega and ATtiny85 are designed-for targets of the emulator
  we already vendor. The hosted toolchain bundle carries only the avr5
  multilib today; adding avr6 (mega2560) and avr25 (attiny85/84) device
  libs is a fetch-script refresh, the same trim done once for the 328.
  Order of attack: ATmega168P (already a live compile target — needs only
  the DEVICE axis + retarget pools; near-free), ATmega2560 (adapter port
  config + pin table D0–D53/A0–A15 + pools), ATtiny85 (the best teaching
  chip: five usable pins, no hardware UART — print REFUSES with the
  reason until a soft-serial story exists; PWM via its own timer1).
* **ESP8266 / ESP32 — reassessed 2026-08-13, still deferred, now with the
  reasons current:** no permissively-licensed Xtensa emulator with SoC
  peripherals exists. The open self-hosted simulator that appeared this
  year is AGPLv3 dual-licensed and runs ESP32 on a GPL QEMU fork; the
  commercial browser ESP32-S3 core is closed. Copyleft engines fit our
  GPL-as-a-service precedent (compile/run oracles, server-side, like SDCC/
  ucsim/simavr) — an ESP target COULD exist as oracle-backed compile-only,
  but the in-browser boundary-D debugger contract would not hold, making
  it a second-class target we choose not to ship half-made. ESP32-C3
  (RISC-V) is the least-blocked future: permissive RV32 cores exist (MIT),
  but the SoC peripheral layer would be ours to build from the TRM — a
  bw-board-scale project. Revisit when a permissive peripheral model
  appears. ESP8266: aging, LX106, nothing permissive — skip outright.
* **The retro tier — ACCEPTED 2026-08-13 (owner), all three chips on the
  roadmap, queued behind the AVR widening.** Architecture decision for
  the 6502 breadboard computer, settled now so nobody re-litigates it
  when the tier opens: **the bus stays inside the emulator.** The real
  build (W65C02S + W65C22 VIA + 28C256 EEPROM + 62256 SRAM + 74HC00
  address glue + 1 MHz can oscillator + HD44780 LCD) is simulated as a
  COMPUTER, not as a netlist: CPU, memory map and address decoding run
  internally at instruction speed the way emu8051/avr8js do — an MNA
  solve per bus cycle at 1 MHz would be both infeasible and pointless.
  Only the VIA's PA/PB pins surface to the electrical bench through
  boundary A, and in the canonical build the LCD hangs off those very
  ports (PB0–7 data, PA5–7 control) — so the LCD is a BENCH PART. We
  already own the HD44780 state machine (char_lcd_i2c decodes one
  behind a PCF8574 backpack); a parallel-mode variant reuses it. The
  EEPROM, SRAM and decoder are the memory map, never electrical parts.
  A "bus inspector" (logic-analyzer view of A0–15/D0–7 with the decode
  boundaries drawn) is a TEACHING feature for later, not simulation
  plumbing. The Eater build's single-step clock button is our debug
  target's insn step — we get his best demo for free, with position
  reporting on top.** The 6502 is the standout candidate in the whole field, and
  it is permissive END TO END: llvm-mos (Apache 2.0 w/ LLVM exceptions,
  actively maintained, strong codegen) or cc65 (zlib — small enough to
  vendor outright) for C; MIT-licensed 6502 cores in JS/TS are abundant
  and tiny. The BOARD story is the W65C02 + W65C22 VIA breadboard
  computer (the Ben-Eater-class build): the VIA's two 8-bit ports are the
  GPIO pins boundary A wants, its T1 timer is the millisecond tick the
  scheduler wants, and the whole peripheral model is a ~200-line 65C22 we
  write ourselves from a simple datasheet — no SoC, no bootloader, no
  closed cores anywhere. Pedagogy nothing else offers: PURE memory-mapped
  I/O on a bus you can see. And the bench session is the best of any
  target: WDC still manufactures both chips NEW in DIP-40 — breadboard
  silicon at 1 MHz with an EEPROM programmer, no SMD, no USB stack.
  **Verification stack refined 2026-08-13 — the 6502 will be our
  best-verified core BEFORE it ships:** the SingleStepTests/65x02 suite
  (MIT) provides 10,000 randomly-generated JSON vectors PER OPCODE with
  full before/after state AND cycle-by-cycle bus activity, specifically
  for the WDC 65C02 — the exact variant still in production and on the
  breadboard. Our core lands only after passing all ~2.56M vectors, a
  conformance bar none of our other cores had at birth. Above it: Klaus
  Dormann's functional test (whole-CPU), perfect6502 (BSD-2, the
  transistor-netlist simulation derived from the real die — the deepest
  oracle in retro computing, CI-side), and Symon (MIT, Java, actively
  maintained) as an independent reference implementation that ALREADY
  models the exact target machine — 65C02 + 6522 VIA + 6551 ACIA in a
  'BenEater' configuration; GUI-bound so not our runtime nor an easy CI
  harness, but a behavior referee when our VIA/ACIA models face a
  datasheet ambiguity. Serial: the machine grows a W65C51 ACIA model
  beside the VIA (both real WDC parts, both in production) so print
  rides real silicon, not an invented console register. bw_now: poll
  T1's IFR rollover between task calls in the scheduler loop —
  ISR-free, the Pico pattern's 6502 spelling.
  **The machine is COMPOSABLE, not fixed — decided 2026-08-13 (owner):
  the tier ships configurations, not one museum piece.** The 6502 system
  emulator is config-driven: a machine is {cpu variant, address-map
  entries (RAM/ROM/VIA/ACIA at ranges, chip-select logic)}. Three ways
  to get a config, one emulator underneath:
  1. **Presets** — DEVICE EATER6502 (the canonical map) and siblings.
  2. **Declared** — the pseudocode's declaration language grows MAP/CHIP
     lines (`CHIP via1 = W65C22 AT $6000`, `MAP RAM $0000-$3FFF`), so
     any bare-6502-plus-whatever build is expressible without wiring.
  3. **Wired** — the pedagogical crown: place the DIP-40 W65C02, the
     VIA, RAM, ROM and a 74HC00 on the virtual breadboard and wire the
     bus BY HAND like the real build; a bus extractor reads the
     designer's netlist, statically solves the glue-gate network over
     the address lines into a chip-select map, and derives the SAME
     config. Your actual wiring determines your actual memory map —
     wire the decode wrong and the derived machine is wrong in exactly
     the way the real breadboard would be. Execution still runs inside
     the emulator at instruction speed (the bus-inside rule stands);
     the wiring is the CONFIG source, not the simulation substrate.
  **MILESTONE 2026-08-13: the core EXISTS and passes everything.**
  bw-board 12edbd6 — our own W65C02 (dependency-free, bus-agnostic,
  instruction-stepped), 2,540,000/2,540,000 vectors across all 254
  testable opcodes of the WDC suite, ground by scripts/grind-w65c02.mjs
  (the 1.1 GB suite stays out-of-repo; clone recipe in the script).
  Decimal ALU, page-cross timing, the WDC bit ops, the undefined-NOP
  matrix, BRK/IRQ/NMI clearing D — all vector-verified. Two facts the
  suite settled against folklore: 0x5C is 3-byte/4-cycle, and WAI/STP
  ship as empty vector files (covered behaviorally in the repo tests).
  **MILESTONE 2026-08-13 (same day): the MACHINE exists.** bw-board
  d189244 — W65C22 (T1 free-run LATCH+2 per datasheet fig 2-4, T2
  pulse-count IFR on reaching zero per fig 2-5, IFR/IER, DDR-masked
  ports, CA/CB edges; SR storage-only by scope), W65C51 (datasheet
  TDRE, real-silicon TDRE bug documented — generated C will pace TX by
  delay, correct on both), and M6502Machine: a machine is a CONFIG
  {clockHz, regions, chips}; EATER6502 is preset #1; overlapping
  decode refuses loudly; shared level-triggered IRQB; pin edges
  surface as {tMs, via1.PA0, level} — the canonical trace shape.
  Verified: hand-assembled ROM blinks PA0 on the exact 1 ms T1 grid,
  serial prints through the ACIA, a T1 interrupt wakes WAI through
  IRQB.
  **MILESTONE 2026-08-13 (same day, part 3): generateC speaks 6502.**
  sb3-creator ff7db49 — DEVICE EATER6502 is the sixth device axis and
  '6502' the fourth emitter core: VIA pins PA0-PA7/PB0-PB6 (PB7
  refused, T1 owns it), level-before-DDR, port A read through $600F
  (no CA-flag side effects), bw_now() harvests T1 IFR6 with no ISR
  anywhere, print paces the ACIA 2 ms/byte and NEVER polls TDRE (the
  WDC silicon bug — delay pacing is correct on buggy and pre-bug parts
  alike). PWM/tone/servo/motor/ADC refuse with reasons. 18 gallery
  examples join by computed dry-run. Found on the way, pre-existing:
  PART (74HC595) programs retargeted "ok" onto every non-8051 device
  while the C silently commented out the output path — compiled, ran,
  did nothing; retarget now refuses PART off-8051 with a reason until
  the bit-banged port lands (a clean fleet lane). OPEN verification:
  compile the emitted C with cc65/llvm-mos and run it on the
  M6502Machine against the referee — the differential that closed the
  Nano and Pico chains; cc65 (zlib) can join the compile service.
  **W65C2x peripheral validation, surveyed 2026-08-13.** The CPU got a
  vector suite; the VIA/ACIA have none, so validation is datasheet +
  cross-implementation + (eventually) silicon. Of the 14 surveyed 6502
  repos exactly ONE helps — Symon's Acia6551 (MIT, real if partial:
  baud-delayed TDRE/RDRF, overrun, programmed reset; wall-clock
  timebase). ksim65 has no VIA/ACIA (generic Timer/ParallelPort only);
  everything else is CPU-only or wrong-system silicon. The REAL find
  is elsewhere: **MAME's 6522via.cpp and mos6551.cpp are BSD-3-Clause**
  (checked in the file headers) — decades of regression against real
  VIC-20/PET/arcade boards, permissive, citable, vendorable with
  attribution. Adopted as the VIA/ACIA reading-reference and
  differential oracle. First cross-read already paid: **MAME and the
  WDC datasheet DISAGREE on T2 pulse-count** — MAME
  (counter2_decrement) interrupts on UNDERFLOW, the N+1th PB6 pulse;
  WDC figure 2-5 asserts IRQB when the count REACHES zero, the Nth.
  Possibly MOS-vs-WDC silicon divergence (MAME's device is the
  MOS/Rockwell lineage). Our model keeps the WDC reading (our part IS
  the W65C22); the discrepancy is DOCUMENTED as an expected diff in
  any MAME differential, and is the first probe for a silicon rig.
  **VR65C02 (MIT, assessed on owner's pointer):** a REAL 6502 with an
  ATmega4809 virtualizing the whole bus — no VIA, a 6551-LIKE UART
  only, so useless for chip-model validation, but valuable twice
  over: its cc65 config/crt0 for a bare custom machine is a working
  reference for exactly the compile-service target the differential
  needs, and its MCU-drives-the-bus pattern INVERTED (a real W65C22
  on a scripted bus) is the design for the silicon oracle rig — the
  retro tier's version of the campaign's hw-oracle suites, and the T2
  question above is its first test case.
  **MILESTONE 2026-08-13 (part 4): the 6502 chain is CLOSED under
  emulation.** sb3-creator 3c538c3 — blocks → generateC → cc65 → 32 KB
  ROM → M6502Machine → canonical trace, AGREE against the referee on a
  three-task program with mid-run button stimulus and paced serial.
  The compile target lives in reference/6502-target/ (cc65 -t none
  --cpu 65C02 + our cfg/crt0 + none.lib; the compile service adopts it
  verbatim when the 6502 lane opens); scripts/diff-6502.mjs is the
  runnable differential. Compilation caught two emitter bugs tests
  could not: 8051 SFRs leaked into the 6502 scheduler tail, and cc65
  -O DISCARDS (void)-cast volatile reads — the T1 harvest now stores
  through a volatile sink. compareTraces gained per-device physics
  budgets (driftPerSecMs, startupMs) for slow machines; order and
  levels stay exact. All six device axes now have a closed chain.
  **The 6502 program corpus, adjudicated 2026-08-13 (owner's survey of
  ~11 source repos).** Tier 2 landed the same hour: the Klaus Dormann
  functional suites (GPL-3, out-of-repo clone) both pass on the first
  run — the NMOS functional test after 30.6M real instructions, the
  65C02 extended-opcodes test after 22M (bw-board f00d762, 52M
  instructions in 1.2 s). Tier 3 is the survey: by license, MIT
  (an assembly crash-course examples repo, a 6502-assembly examples
  repo) and CC-BY-4.0 (a homebrew 65C02 computer whose machine —
  65C02 + 65C22 + 65C51N + 32K/32K — is a near-sibling of EATER6502
  and becomes our second machine PRESET) are publishable corpus with
  attribution; everything unlicensed (a beginners-book repo, a
  C/asm/Forth robot game, three C64 example collections, a Neo6502
  examples repo, a 6502+TFT breadboard project, an emulator+assembler
  repo) goes to the research corpus, LOCAL ONLY, exactly the 8051
  pattern. The mechanism that makes ANY 6502 binary a CPU test
  regardless of its target machine: the TWIN-RUN harness — flat 64K
  RAM on both sides, our core vs an independent permissive core
  (lib6502/run6502 MIT, or py65 BSD), lockstep state comparison,
  instruction-capped so machine-specific busy-waits bound instead of
  hang; C64/NES-specific I/O reads open-bus identically on both sides.
  That harness is the corpus lane's next build. That harness EXISTS the same day
  (bw-board, twinrun-6502.mjs + a small C peer): our core vs vrEmu6502
  — the MIT, W65C02-capable emulator library adopted from the owner's
  second survey (it also powers an MIT homebrew machine whose display
  chip library, a TMS9918A model, is noted for the tier's future video
  peripheral). Both Dormann suites AGREE in lockstep over 52.6M
  instructions including per-instruction cycle counts, under three
  documented exemptions each adjudicated by the vector suite: the B/U
  in-register convention, BBR/BBS taken cycles (vectors 6, peer 5),
  and $5C (vectors 4, peer models the folklore 8). Rest of that
  survey: a WTFPL python 6502 and the C#/TS/C emulators are redundant
  with stronger adopted pieces; an unlicensed Java Ben-Eater emulator
  is research-tier only; a CBM emulator needs non-redistributable
  C64 ROMs — the flat-RAM twin-run approach avoids ROM entanglement
  entirely.
  **MILESTONE 2026-08-13 (part 5): the DECLARED machine works end to
  end.** sb3-creator — MAP RAM/ROM ranges and CHIP <name> = W65C22/
  W65C51 AT $addr parse (overlap, duplicate-kind and wrong-device
  refusals), decompile, survive the @bw header, and rebuild through
  the C reader; generateC moves the register bases with the declared
  addresses and warns when a declared machine lacks the W65C22
  timebase. The differential proves it: a machine with the VIA
  renamed and moved to $7000 and the ACIA at $4400 AGREEs with the
  referee through cc65 exactly like the Eater preset. Config source
  #2 of 3 is real; the wired-breadboard extractor (#3) emits exactly
  these lines, and the ld65 cfg generated from MAP ranges is the
  remaining piece of full memory-shape freedom. **DONE the next day:**
  generate6502LinkerCfg(machine) emits the config from declared
  regions (preset for null; preset output tested equal to the
  checked-in eater.cfg numbers), with the 6502's own constraints as
  refusal reasons (RAM at $0000 through $02FF minimum; ROM covering
  $FFFA). The differential AGREEs on three shapes including an 8K-RAM
  / 16K-ROM-at-$C000 machine with chips at $A000/$A400. What remains
  of the composable tier: the wired-breadboard extractor (source #3)
  and the mike42 preset. **The extractor landed the same day
  (bw-board 31e8a35): config source #3 is REAL.** The designer netlist
  — real DIPs plus 74HC00 glue, wired by hand — has its decode solved
  by evaluating every chip's select condition at all 65536 addresses
  through the NAND network; out come the SAME MAP/CHIP lines the
  grammar takes, and the canonical two-gate-package decode ran the
  full differential (cc65 → machine → referee) and AGREED. Refusals
  with addresses named: bus contention, open vectors, floating
  selects, permuted address buses, shorts, non-contiguous windows;
  mirrors and open-bus holes are notes. Wire the decode wrong and the
  machine is wrong exactly as the bench would be — now demonstrably.
  All three config sources are one. The fleet meanwhile wired
  eater6502 into the app (bw-board e14d226: factory route, adapter,
  debug target, board kind, 7 oracle tests). Remaining: the mike42
  preset, and the designer-UI face for the wired flow.
  **MILESTONE 2026-08-14: BBC BASIC IS ALIVE ON THE MACHINE.** The
  owner pointed at BeebEater (MIT) — a BBC BASIC port targeting
  byte-for-byte the EATER6502 preset — and it boots on our machine to
  its banner, answers PRINT 2+2 with the BBC's right-aligned 4, and
  stores and RUNs a FOR loop typed over the ACIA
  (bw-board 6978656, scripts/beebeater-smoke.mjs; the BBC BASIC 4 ROM
  is Acorn heritage — run locally, never vendored, the ehBASIC rule).
  Real software, written for real silicon, indifferent to which it is
  on: the strongest whole-system evidence the tier can produce short
  of the bench. Two honest findings: without an LCD the boot hangs
  polling the HD44780 busy flag on a floating PB7 — exactly as the
  real breadboard would (the briefed HD44780 part model will replace
  the port-B-low workaround); and post-CR keystrokes need pacing or
  BeebEater's line handling drops characters.
  **Graphics without video hardware, proven 2026-08-14: the VDU
  stream.** The owner asked about a BBC-Micro square-drawing tutorial;
  its whole technique is the MOS OSWRCH byte protocol (MOVE/DRAW/PLOT
  are layers over VDU 25 sequences). Our machine already speaks it:
  BBC BASIC's own DRAW commands, typed at the BeebEater prompt, emit
  the exact 6-byte PLOT sequences over the ACIA, and a 20-line decoder
  recovered the closed square path. So the graphics lane for the whole
  BBC family is a CLIENT-SIDE VDU TERMINAL (canvas interpreter for the
  documented VDU codes) — no video-hardware emulation, and the same
  pane renders BASIC programs and assembly tutorials alike. The
  tutorial page itself is unlicensed (technique is fact, code stays
  unread-for-implementation).
  **Microsoft BASIC-M6502 is MIT — the SHIPPABLE BASIC (adjudicated
  2026-08-14).** Microsoft's archival release of the 1976-78 BASIC 1.1
  source (6,955 lines, conditional targets: Apple/PET/OSI/KIM) is MIT
  — unlike the BBC BASIC 4 ROM (Acorn heritage, local-only) and
  ehBASIC (NC). Port lane: adapt the source to ca65 with OUR OWN tiny
  ACIA I/O shim → a BrickWright-buildable BASIC ROM that can be
  VENDORED AND SHIPPED. The product options, in order of leverage:
  (a) ship the MIT BASIC machine in the app — breadboard-wired 8-bit
  BASIC computer with terminal pane, fully permissive end to end;
  (b) the VDU terminal above, giving that machine (and BeebEater
  locally) real graphics; (c) an assembly lane — editor card +
  ca65/vasm on the compile service → ROM → machine → debugger, with
  tutorials as guided examples; (d) a blocks→BASIC bridge: generate
  BASIC from single-script block programs and TYPE it into the booted
  interpreter over the ACIA — your blocks executing inside a 1977
  interpreter (multi-WHEN refused honestly: BASIC is single-
  threaded); (e) the BASIC interpreter as a twin-run CPU workload,
  millions of real-code instructions of differential.
  **ALL FIVE OPTIONS GREENLIT (owner, 2026-08-14) — and BBC BASIC
  itself turns permissive.** The R.T. Russell lineage is Zlib across
  the board (verified in the repo licenses): the original Z80 BBC
  BASIC, the SDL/console edition (C — adopted as the future
  generateBASIC oracle: run generated BASIC under it as ground
  truth), the Spectrum Next port, the eZ80 Agon port, and PicoBB —
  BBC BASIC for the Pico, which our EXISTING RP2040 chain may boot
  as-is (fleet lane opened). So the language is shippable via
  Russell even though Acorn's 6502 ROM stays local-only. A BBC Micro
  dev environment under Apache-2.0 joins as corpus tooling; an MIT
  Rust BBC Model B emulator (boots MOS 1.20 — ROMs are the user's
  own) is the rendering cross-check for the VDU terminal, never a
  vendored runtime. Three unlicensed BBC games/asm projects join the
  local research corpus. Fleet lanes seeded the same hour: the MS
  BASIC ca65 port with our own ACIA shim (the shippable BASIC), the
  HD44780 part model (datasheet clean-room), and the PicoBB boot.
  The coordinator's next contract: generateBASIC (bbc + ms profiles,
  single-script programs, honest multi-WHEN refusal) and
  basicToPseudocode — the AST bridge the owner named.
  **generateBASIC SHIPPED the same day (sb3-creator fedf31f) — and it
  runs TRUE inside the interpreter.** Blocks → line-numbered BASIC,
  typed live into BBC BASIC on the 6502 machine: prints 1,2,3,99 and
  blinks PA0 at 500 ms ±2 through a DEF PROC call with a parameter
  (the chapter-16 contract; DEF FN is the reader's obligation, stated
  in the contract). Profiles: bbc (TIME waits, EOR toggles, ?& VIA
  pokes from the machine config) and ms (POKE/PEEK, GOTO loops,
  two-significant-char names with a REM legend, delay-calibration
  constant). Empirical toolchain fact that shaped naming: BeebEater
  UPPERCASES serial input and BBC's conditional tokenizer eats exact
  keyword matches — count= broke as COUNT=, COUNTX ran fine — so
  names collide against the full keyword set case-blind, measured on
  the live machine. scripts/diff-basic.mjs is the standing live
  differential. caterpillar-assembler (a 1983 type-in rewritten in
  6502 asm) checked: MIT — PUBLISHABLE corpus, not just research.
  Next in the lane: basicToPseudocode (PROC and FN both, per the
  owner's chapter-16/17 directive). **DONE the same day (sb3-creator
  6f51cb8): both directions exist and close.** The reader takes
  numbered or structured BASIC; maps assignments, PRINT, FOR/NEXT,
  REPEAT/UNTIL (BBC's post-check kept honestly — first pass
  duplicated ahead of a pre-check loop, with the comment saying why),
  WHILE/ENDWHILE, IF forms, DEF PROC with parameters, and single-line
  DEF FN by macro-expansion at call sites; NAMES everything else as a
  `# BASIC:` comment plus warning. Round trip: structured emit → read
  → emit is BYTE-IDENTICAL. En route the emitter's own repeat_until
  was found POST-check where Scratch's contract is pre-check — fixed
  (WHILE NOT structured, guarded GOTO numbered). Corpus: the CC0
  examples repo is the fixture set (three programs vendored with
  attribution; the full 35-file corpus reads at 64% of ~2000
  statements, rest named — the gaps are graphics/CASE/DIM, i.e. the
  VDU-terminal and arrays lanes). Two unlicensed example repos and
  the chibiakumas sources joined the LOCAL research corpus with
  provenance notes.
  **The VDU terminal's logic half EXISTS (bw-board 02b01fe), and the
  fleet's HD44780 landed.** The VDU decoder turns the ACIA stream into
  typed events (move/draw/plot signed-16-bit, mode, colour, origin,
  cls, text; unknown codes surface, nothing drops), state across any
  chunking; its live test types a DRAW program at the BeebEater
  prompt and asserts the decoded closed square. The fleet's HD44780
  behavioral part (datasheet clean-room, 21 oracle tests) is wired to
  VIA port B in the BeebEater smoke with busy-flag read-back — the
  LCD now SHOWS the interpreter's screen ("HI3" / ">"), asserted.
  What remains of the terminal is the canvas face in the app (fleet,
  after bundle frees). bw-board recycled onto the mike42 preset
  (HB6502, CC-BY facts, banked-ROM gap to be reported not hacked);
  the MS BASIC port session was found crashed at launch (wrong cwd)
  and relaunched; PicoBB still in flight.
  **The third executor and the day's best catch (sb3-creator
  fcd77af).** BBCSDL's console edition (zlib) built locally and now
  serves as the HOST oracle: generated numbered BASIC stored over
  stdin, RUN, printed values compared to the referee — values only
  (host time is wall time), hardware-gated (host pokes would hit the
  interpreter's own memory). Its FIRST run caught two bugs at once:
  the oracle script treated a refusing referee as passing (fixed —
  refusal is a blocked comparison), and behind that, the referee had
  NO custom-block support: Russell's interpreter printed the
  procedure's result and took the right branch while the referee
  skipped the call and took the wrong one. The referee now binds
  parameters per frame (nested calls shadow); regression-locked.
  Three executors print the same values: referee, BBC BASIC 4 on the
  machine, BBCSDL on the host. CI: sb3-creator was red on ONE lint
  error — fixed, green (run 31745330598); brickwright-lite and
  emu8051 were already green (the bundle's EATER6502 wiring merged
  with a passing build); bw-board/ucsim/bw-parts have no workflows.
  The GUI BASIC tab + remaining CI + GH Pages/Vercel deploys are
  delegated with a full spec (owner directive).
  **PicoBB BOOTS (verified, ucsim-stc 6a33d2c): BBC BASIC on the
  Pico tier.** The fleet booted Memotech-Bill's PicoBB (zlib) under
  rp2040js to the banner and PRINT 2+2 — with three fixes that
  improve the whole RP2040 stack: the real B1 bootrom must be staged
  (PC otherwise slides through zeroed ROM), rp2040js's unimplemented
  SIO reads return 0xFFFFFFFF which makes FIFO_ST.VLD stick and
  multicore polls spin (stub: RDY=1/VLD=0), and PicoBB blocks on an
  ANSI cursor-position probe (answer ESC[24;80R). Build with
  SOUND=NONE: the SDL sound module launches core 1, which deadlocks
  single-core emulation. The owed labwired validation rows landed in
  the same handoff (nRF52840 deep/silicon-verified; ESP32-C3
  reset-state only, needs a runtime differential before oracle use).
  The corpus campaign also advanced quietly: layer-5 RP2040 sweep,
  and simavr vs avr8js AGREE on the AVR self-timestamping
  differential. Meanwhile the mike42 lane already shows an ehBASIC
  'Ready' prompt computing sums, and the MS BASIC translation is
  mid-flight. BBC BASIC now runs on BOTH tiers: the 1 MHz 6502
  machine and the 125 MHz Pico.
  **Russell-on-6502, adjudicated honestly (owner's question,
  2026-08-14): NO — and the reason starts the Z80 tier.** Russell
  never wrote a 6502 BBC BASIC: his lineage is Z80 assembly, x86,
  and the portable C interpreter (PicoBB is that C interpreter — so
  the Pico boot already IS Russell's BBC BASIC on our stack). On the
  6502, BBC BASIC means Acorn's ROM: running today via BeebEater,
  local-only forever. The C interpreter cannot fit a 64K 6502 and a
  hand-port of 16K of Z80 assembly is months of expert work — so the
  shippable-BASIC slot on the 6502 stays with the MS BASIC port (in
  flight), and the shippable BBC BASIC slot on a RETRO machine is
  the Z80 TIER, where BBCZ80/next-bbc-basic (zlib) run natively.
  Provisioning verified: SingleStepTests/z80 is MIT (the same
  vector-suite lineage, undocumented flags included) and BBCZ80
  ships prebuilt CP/M binaries beside its zlib source. The plan is
  the 6502 playbook verbatim: own Z80 core ground against the
  vectors, twin-run peer (the MIT multi-arch C++ emulator noted
  earlier gets its maturity look now), composable machine, and a
  minimal CP/M console shim (BDOS at $0005 — BeebEater's mini-MOS
  pattern) so bbcbasic.com boots to its prompt over the ACIA-
  equivalent. Vector suite cloning; the core is the coordinator's
  next contract.
  **DONE THE SAME DAY (bw-board 620b9c7): the Z80 core is
  VECTOR-COMPLETE — 1,604/1,604 files, 1.6 MILLION vectors, zero
  failures.** Scaffold to complete in one arc (180 → 252 → 508 → 588
  → 1,604): main page with the precise MEMPTR rules, CB first-grind
  green, ED including the interrupted-repeat flag rules DERIVED FROM
  THE VECTORS and pre-validated 3,990/3,990 in a throwaway script
  before touching the core, DD/FD substitution pages with the
  undocumented index halves and the DDCB no-M1 sub-opcode, and one
  last vector-established subtlety: a DD/FD prefix clears the Q
  consideration for SCF/CCF. Two cores now stand vector-verified end
  to end (W65C02 2.54M; Z80 1.6M). Interrupt delivery is the machine
  layer's job, next — along with the CP/M console shim and
  bbcbasic.com: Russell's own BBC BASIC, zlib and shippable, on our
  second retro machine.
  **AND IT BOOTS, the same hour (bw-board): BBC BASIC (Z80) v5.00 —
  "(C) Copyright R.T.Russell 2025" — on our vector-complete core over
  a minimal CP/M BDOS shim.** Banner, PRINT 2+2 → the right-aligned
  4, a stored FOR program RUNs: 55.9M cycles of real interpreter
  code as the core's first whole-system workout. Zlib from
  interpreter to core: the SHIPPABLE BBC BASIC the owner asked
  about, on our own retro machine. Findings: BDOS returns must land
  in A and L both; BASIC flushes type-ahead after Enter (prompt-
  paced entry, BeebEater's lesson by another mechanism). Next: the
  composable Z80 machine proper (serial console, interrupt delivery)
  and the app experience.
  **Naming & licensing policy for the shipped interpreters
  (adjudicated 2026-08-14, owner's question).** Two separate regimes:
  zlib governs the CODE (ship/modify/fork freely; mark alterations;
  keep notices), the BBC's trademark governs the NAME — Russell's
  permission to call his work "BBC BASIC" does NOT transfer to forks.
  BrickWright's rules: (1) ship his interpreters VERBATIM and keep
  every adaptation in OUR layer (shims, machine, terminal — already
  the architecture; PicoBB's SOUND=NONE is his own build option, not
  a patch); (2) UI describes, never brands: "BASIC console — runs
  BBC BASIC (Z80), © R.T. Russell, unmodified"; the tab is plain
  "BASIC" with the dialect in the profile toggle; (3) if his source
  is ever patched, mark it altered (zlib) AND rename it (trademark) —
  avoid by design; (4) the same discipline for the MS port: MIT code,
  but our ca65 derivation ships as basic-m6502-bw "derived from
  Microsoft's MIT-licensed 6502 BASIC source", never as "Microsoft
  BASIC" the product; (5) NOTICE/LICENSES entries in the shipping
  repos carry the attributions — added to the deploy lane's
  checklist. (Engineering policy reading, not legal advice; product
  naming can get counsel before launch.)
  **MICROSOFT BASIC V1.1 IS ALIVE ON THE 6502 MACHINE (verified
  2026-08-14): the fully shippable 6502 BASIC.** The fleet's ca65
  port (basic-m6502-bw — named per the trademark policy, THIRD-PARTY
  attribution in place) boots on M6502Machine to the 1978 dialogue:
  MEMORY SIZE? / WIDTH? / 15871 BYTES FREE / OK — answers PRINT 2+2
  and runs stored programs, all six smoke checks green, verified by
  the coordinator locally. The interpreter roster is now complete
  across the stack: 6502 = Acorn BBC BASIC (local-only) + Microsoft
  BASIC 1.1 (MIT, SHIPPABLE); Z80 = Russell BBC BASIC (zlib,
  shippable); Pico = PicoBB (zlib, shippable); host oracle = BBCSDL.
  Meanwhile the full-Scratch-surface translator carries acceptance
  tests for say/ask/operators/pen/motion/multi-WHEN/stop with named
  degradations, and the licensing labels are being applied to the
  deploy lane.
  **The Acorn ROM question, chased to the root (owner's question,
  2026-08-14): there is NO license anywhere in the chain.** BeebEater
  (MIT) credits J.G. Harston; Harston's site ships the ROMs with no
  stated permission; jsbeeb documents nothing either. The scene's
  entire practice rests on the informal late-1990s Pace Micro
  Technology permission for emulator use ("by kind permission of
  Pace" in BeebEm-era credits) — never rescinded, never renewed,
  rightsholders since moved (Pace → Arris → CommScope). How they
  "manage": tolerated abandonware plus one traceable informal
  permission. It is not a license. Three postures for BrickWright:
  (a) CONSERVATIVE (current doctrine) — local-only; costs nothing,
  since the shippable slots are filled (MS BASIC/MIT on the 6502,
  Russell/zlib on the Z80 and Pico); (b) SCENE-STANDARD — ship with
  a ROMS-NOTICE citing the Pace permission, defensible by precedent
  not by document, an OWNER decision given commercial posture;
  (c) BRING-YOUR-OWN-ROM — the app ships the BeebEater machine with
  an empty ROM slot the user fills (upload/URL), 100% clean,
  standard emulator practice. RECOMMENDED: (c) as the built default,
  (b) as an owner-flippable option later. The BYOR slot is app-lane
  work (goes to the fleet with the VDU/terminal wiring).
  ****Debugger disassembly, audited 2026-08-14 (the owner remembered
  right): the 8051 HAS it, nothing else does.** emu_disasm in the
  8051 debug target, held to the campaign's standard — verified
  against stc_disasm.py's independent table, 237 instructions, 0
  disagreements (debugger-ui.md §). The avr8js, rp2040js and
  eater6502 debug targets have none. The fill, two mechanisms by
  ownership: OWNED-CORE targets (6502, Z80 — and the machines built
  on them) get LIVE client-side table disassemblers, coordinator
  work paired with the cores; instruction LENGTHS come pre-proven by
  the vector suites' pc-deltas (2.5M + 1.6M vectors), mnemonics
  cross-checked against published tables — and live disasm works
  even on hand-poked memory, which a listing cannot. TOOLCHAIN
  targets (AVR, ARM) get SERVICE-SIDE listings: both toolchains ship
  objdump, so the compile service returns a source-interleaved
  disassembly map per build and the debugger fetches it once — the
  same artifact the assembler lane's R1/R3 need, one mechanism
  serving both. (Live ARM disasm later via capstone-wasm if wanted;
  AVR is not in capstone, so the objdump path is the right one
  there.)
  **The ASM tab, designed 2026-08-14 (owner's question): a view-and-
  run lane, not a sixth generator.** A client-side blocks-to-asm
  emitter would LIE (not the asm that runs); the tab instead shows
  the TOOLCHAIN'S OWN output for the generated C, via the compile
  service: sdcc's .asm/.rst carries C lines natively (8051 + Z80),
  avr/arm use objdump -dS of the ELF already built — the SAME
  artifact as the queued disassembly lane and the C line tables,
  one service change wearing three hats — and cc65 -T -S for the
  6502. Client: thin async tab, cache-by-hash, read-only, current-PC
  highlight when the debugger runs. The asymmetry is deliberate and
  STATED in the UI: no ASM-to-blocks arrow — the import direction is
  'assemble & run on the machine' (the R2 editor lane). Service
  response shape v1 sent to the queued cfront lane:
  { asm, lineMap, format, v }.
  **Source-level debugging for C and BASIC, designed 2026-08-14
  (owner's question).** One shape everywhere: a source map plus a
  position signal, and every language already has both unharvested.
  C: sdcc's .cdb (the format ucsim itself consumes) for the 8051;
  cc65 -g + ld65 --dbgfile line/span records for the 6502 (pairs
  with symbolsFromLd65Labels — one build, three artifacts); gcc -g +
  objdump --dwarf=decodedline for AVR/ARM — an EXTENSION of the
  already-queued objdump service lane, not a new one. Shared
  mechanism: LineTable {addr→(file,line)} + stepLine / lineBreakpoint
  helpers over the existing PC breakpoints. BASIC: TRACE ON / TRON
  print executed line numbers INTO the serial stream we already
  capture — a version-robust position signal with zero interpreter
  surgery (the verbatim-ship rule holds); breakpoints = watch the
  stream for the marker, halt the machine. The crown: generateBASIC
  records a line↔block map during emission (the @bw-yield-map move),
  so the TRACE stream drives BLOCK highlighting — Scratch blocks
  lighting up as a 1981 interpreter executes them. Maps COMPOSE:
  block-level position works uniformly across the C scheduler and
  the BASIC interpreters; the user picks altitude — blocks, source
  line, or disassembly. Lanes: line-table parsers ×3 fleet-sized;
  the generateBASIC line↔block map is coordinator (emitter) work.
  **The assembler lane, designed 2026-08-14 (owner's question).**
  Key fact: every target's assembler is ALREADY in the compile
  service (sdas8051 + sdasz80 ship inside sdcc; GNU as inside
  avr-gcc / arm-none-eabi; ca65 is the 6502 chain) — this lane is
  wiring, not toolchain work. Rungs: R1 read-only Assembly tab
  showing the toolchain's asm of the generated C (one -S flag,
  source-interleaved listing — the missing rung between the C tab
  and the debugger); R2 the asm INPUT editor — assemble via the
  service, load the raw binary onto the machines we already boot
  (every smoke this week used exactly that loader path), serial and
  pins out; R3 the asm debugger — .lst PC↔line mapping into the
  existing debug-target factory, registers + stepping the cores
  already expose; R4 inline ASM: blocks in the dialect lowering to
  each C emitter's native inline assembly (sdcc __asm / gcc asm
  volatile / cc65 asm()) — the replace-one-block teaching bridge;
  R5 BeebAsm dialect compatibility (GPL-3, service-side) and the
  harvested asm corpora as loadable examples with the twin-run
  harnesses as oracle. R1+R2 are fleet-sized; R3 rides the debugger
  contract; R4 touches the emitters (coordinator).
  **Blinkenrocket, adjudicated 2026-08-14 (owner's question): YES,
  and it is a gem.** The congress badge — ATtiny88 @ 8 MHz, 8×8 LED
  matrix, two buttons, animations loaded through the AUDIO JACK by a
  Hamming-FEC modem, stored in EEPROM. The firmware (the owner's
  fork of blinkenrocket-firmware) is dual LGPL-3 OR 3-CLAUSE BSD —
  the BSD option makes the whole experience vendorable and shippable.
  The build plan rides entirely on existing patterns: (1) an ATtiny88
  config for the chip-parameterized avr8js adapter (the fleet already
  did ATtiny85/ATmega2560 this way; tiny88 is a 328-family core —
  EEPROM peripheral required, storage.cc lives on it); (2) a
  matrix8x8 board part with persistence-of-vision duty integration
  (the behavioral-display pattern: neopixel/bargraph/HD44780 all
  exist); (3) the badge as a preset circuit in the ladder lane;
  (4) the crown, stage 2: the MODEM — V2 firmware samples sine audio
  via ADC, and our stimulus system speaks volts-over-time, so the
  blinkenrocket.de web editor's own encoding can drive animation
  uploads INTO the simulated badge. avr-gcc -mmcu=attiny88 joins the
  compile-service bundle trivially. Queued as a fleet lane when a
  session frees.
  **The staged-build ladder, adjudicated 2026-08-14 (owner's
  three-article question).** The makerhacks stages, assessed:
  (1) FREE-RUN (Z80, data bus tied low = NOP, LEDs on address lines):
  runnable TODAY — the extractor can even DERIVE the tied bus from
  the wiring; needs only a small adapter exposing the address lines
  as pin states so the LEDs count. (2) PICO-AS-MEMORY (Pico serves
  ROM/RAM/clock to the Z80 over GPIO): fully REPRESENTABLE in the
  designer; runnable today in SEMANTIC form (the memory the Pico
  serves IS the machine's RAM array — same behavior, stated
  honestly); true pin-level two-CPU co-simulation (we emulate both
  sides!) is the parked supervisor-pattern moonshot, beside the
  SAP-1 — our instruction-stepped Z80 core does not expose T-states,
  which is the real boundary. (3) OUT-TO-LCD (Z80 OUT snooped to a
  1602 I2C LCD): runnable today via a port-write hook driving the
  existing I2C-LCD part; the Uno-snooping full-fidelity form joins
  the two-CPU moonshot. THE PRESET LADDERS for the circuit designer
  (each stage a saved circuit, Couch-To-64k/Eater pedagogy):
  Z80: free-run counter → Pico-memory hello → OUT→LCD → ROM+RAM+
  latched LED port → SEARLE serial → CP/M machine. 6502: Eater
  free-run ($EA tied) → ROM blink → +VIA blink → +HD44780 hello →
  +ACIA serial (BeebEater via BYOR slot) → full machine. Honorable
  mentions for later stages: TEC-1-style hex keypad + 7-seg (parts
  exist), KIM-1-style. Preset circuits = bw-circuit-ui lane once the
  Z80/MC6850 DIPs land; the address-line adapter + port-write LCD
  hook = small bw-board additions.
  The Z80 breadboard-scene survey + THE MACHINE (2026-08-14, owner's
  two rounds of pointers).** The lineage is singular: nearly every
  breadboard Z80 — the makerhacks free-run, the PainfulDiodes BeanZee
  (whose MIT examples we already carry), hackaday builds, Fort
  Collins, the Arduino-supervised variants — descends from GRANT
  SEARLE's minimal 7-chip design (ROM low, RAM high, MC6850 ACIA at
  ports $80/$81), which is also RC2014's ancestor. New MIT finds from
  the second round: Bread80's Couch-To-64k (a STAGED Ben-Eater-style
  Z80 tutorial — pedagogically ideal for designer presets) and
  trevor-makes' avr-z80 (the AVR-supervisor pattern, which our stack
  could uniquely model as a TWO-CPU circuit someday — we emulate both
  sides). The composable Z80Machine is BUILT (bw-board): the 6502
  pattern with the Z80's twist — chips in PORT space (IORQ), regions
  in MEMORY space (MREQ); SEARLE preset; our own MC6850 from the
  datasheet; IM 1 delivery in the machine layer (RST $38, HALT wake,
  EI deferral) — tests boot a hand-assembled ROM that prints and
  echoes, and an RX interrupt wakes HALT through the handler. Designer
  parts to brief: Z80 DIP-40 + MC6850 DIP-24 (62256/28C256/74HC00
  reuse); the bus extractor's Z80 extension = the MREQ/IORQ split.
  Software story per machine: Searle's NASCOM BASIC ROM is Microsoft
  heritage (local-only, BYOR slot like the Acorn ROM); the SHIPPABLE
  path stays CP/M + BBCBASIC.COM (CP/M 2.2's 2001/2022 releases) and
  Zeal 8-bit OS (Apache-2.0) as a future native preset.
  **The display leg, adjudicated 2026-08-14 (owner's third survey).**
  Order of battle: (1) the HD44780 CHARACTER LCD first — it is the
  canonical breadboard build's own hello-world (RS/RW/E on PA5-PA7,
  data on PB0-PB7, i.e. a BOARD part wired to VIA pins through the
  netlist, not a bus chip — one-board-one-truth already owns this
  shape). bw-board has neopixel/bargraph and an I2C-backpack LCD but
  no parallel HD44780: the model is written from the HD44780 datasheet
  (4-bit and 8-bit modes, busy flag, DDRAM addressing), reusing the
  I2C part's character logic; a cycle-level MIT JS emulator that
  models the same LCD (diodesign) serves as cross-reference, never as
  source. (2) THEN the TFT tier the owner wants (a 6502+TFT
  breadboard project exists but is UNLICENSED): clean-room again —
  the controller (ILI9341-class) is modeled from its public
  datasheet, wiring facts may be read, the project's BIOS/game code
  stays unread by implementing agents; the LED-cube precedent is the
  procedure. Also from the survey: a fresh MIT modular C++ emulator
  (Z80 + 6502/6507, headless CLI + Qt host) is NOTED as a candidate
  Z80 twin-run peer for when the Z80 core starts — young (2 stars),
  assess maturity at adoption time, not now. A 6502 wristwatch (no
  license) is a curio; BBC/Electron sprite routines (AGPL) and two
  unlicensed game/project repos join the LOCAL research corpus as
  twin-run inputs, never redistributed. ehBASIC (NC license):
  never vendored, never shipped — but "boots to the READY prompt over
  the ACIA" is a legitimate LOCAL validation milestone on the mike42
  preset, the retro tier's deepest whole-system smoke test, feasible
  once nothing more than the preset exists. The 6502+TFT project also
  seeds a future machine peripheral (TFT + shift-register controller
  on the Eater bus) — hardware pattern noted, code unlicensed. Found on the way: Symon's 6522 is an unimplemented stub —
  Symon stays a CPU/machine referee, the W65C22 datasheet is the
  peripheral authority. Next: generateC '6502' core in sb3-creator
  (bw_now off T1 IFR polling), the MAP/CHIP declaration grammar, the
  bus extractor over the designer netlist.
  **Ecosystem triage 2026-08-13 (owner's survey of 13 more
  implementations):** none displaces the own-core-plus-vector-suite
  plan, two join the stack: run6502/lib6502 (MIT, headless C CLI) as a
  fast third CI executor beside perfect6502's slow depth, and py6502
  (BSD-2) whose Python assembler gives test tooling an assembler
  without invoking the full toolchain. Symon remains the machine-level
  reference. Excluded and why: GPL3/GPL2 entries (easy6502's core,
  cpu6502, mini65-sim — vendoring barred, and the oracle bench is
  already stronger on MIT/BSD), a non-commercial-clause simulator
  (not permissive, out entirely), NES- and Atari-shaped machines
  (wrong system), JVM/GUI apps and on-Arduino curios (wrong runtime;
  ksim65 noted as a second JVM reference if ever needed).
  Second in the tier: the Z80 — SDCC on OUR OWN compile service already
  targets it (the compile side is nearly free), permissive cores are
  everywhere, and the RC2014 ecosystem is the same teaching energy; its
  peripheral story (PIO/CTC) mirrors the VIA build. Third, the modern
  counterpoint: CH32V003 (ten-cent RISC-V silicon, RV32EC is trivial to
  emulate permissively, simple documented peripherals) — worth a
  feasibility note when the tier opens.
* **The 6502 breadboard-scene survey, adjudicated 2026-08-14 (owner's
  sixteen-source round: awsh, Cornell KiT, skrasser, Wilson primer,
  cool-web.de, two hackaday builds, Booth's blogmywiki series, Hamann,
  Vectron 64, sixty5o2, the single-breadboard Reddit build).** Four
  research agents read everything including repo LICENSE files. THE
  LICENSE LANDSCAPE IS NEAR-UNIFORM: one shippable item in sixteen
  sources. **sixty5o2 (Jan Roesner, MIT, clean) SHIPS VERBATIM** — a
  1.5 KB bootloader/monitor for exactly the EATER6502 shape (VIA $6000,
  LCD 8-bit on port B, user programs at $0200, upload = Arduino drives
  PORTB + pulses /IRQ 30 µs/byte, VIA interrupts off — the IRQ is a
  bare wire). It becomes the EATER6502 preset's payload ROM, the
  tethered-upload reference, and a 1.5 KB IRQ/LCD-timing compat test.
  Everything else: no license (KiT, Vectron, Booth, awsh, 8bitflynn,
  Wilson, 6502Nerd's breadboard ROM), CC BY-NC (Nielsen's 6507SBC —
  blocks files, not facts), or expressly forbidden (cool-web.de's
  Impressum). Research-only; memory maps and decode equations are
  uncopyrightable facts we encode freely. dflat's Oric branch is MIT —
  the language could port to our machines someday; the kit-emu Java
  emulator is an unlicensed read-and-compare oracle.
  **MACHINE VERDICTS:** five of the sources ARE EATER6502 (awsh,
  8bitflynn, Hamann, Booth's base machine, sixty5o2's host) — the
  preset is confirmed as the community's canonical shape, no action.
  Wilson's primer is EATER'S ANCESTOR (Eater credits it): a WILSON6502
  preset is a rename-plus-extras (74HC132 decode, multi-VIA/ACIA
  expansion at $5000/$4800/$4400..., anti-555 clock doctrine, RAM CS
  qualified by Φ2); his alternate config ≈ HB6502 — zero new parts,
  cheap win. KIT1 (Cornell Tomlinson) is the third preset: RAM
  $0000-$6FFF, VIA $7800, 16C550 UART $7820 (chosen over the 6551 for
  the ACIA transmit bug — the one new high-reuse part), ROM
  $8000-$FFFF, 1 MHz; its dual-port VRAM + MC6847 video and PS/2
  shift-register chain are parked big-ticket. **THE GEM: Nielsen's
  6507SBC** — four ICs (R6507, 6532 RIOT at $0080, W27C512, one 74HC04
  gate: A12 = the ENTIRE decode), the ideal stage-0.5 machine between
  free-run and EATER6502. Needs the 6532 RIOT model (128 B RAM, two
  ports+DDRs, ÷1/8/64/1024 timer, PA7 edge detect) and a 6507 mask on
  the core (A13-A15 amputated, no IRQ/NMI pins, NMOS opcode surface);
  clean-room from schematic facts. Vectron 64 (research-only) is the
  "I/O without an I/O chip" curriculum: LCD-on-databus with
  address-strobe E, PS/2 via 595/161/244 capture chain, 74HCT688
  full-address decode. 6502Nerd's maximal machine (TMS9918A +
  AY-3-8910 + banking) and skrasser's FPGA hybrid stay research-only;
  salvage from the latter: the bare memory-mapped LED port and the
  VIA-timer marquee exercise.
  **THE LADDER, REFINED** (Booth's series + cool-web's 25 rungs + KiT's
  stages independently converge on our design; merged order): E0 clock
  module alone (555 astable + single-step; clock-source selector as a
  simulator control) → E1 CPU-alive: straps + status LEDs
  (clock/RW/SYNC/VPB) + address-nibble LEDs, free-run $EA, watch $EAEA
  climb → E1.5 static-clock register-retention (the cool-web
  genuine-vs-counterfeit W65C02 story) → E2 ROM-only + data-bus LEDs,
  single-step verifying every byte; the write-to-unmapped-$6000
  visible-then-vanishing store is the teachable moment (Booth) → E2.5
  alt-track: the 6507SBC four-chip machine → E3 latch LED port
  (74HC374 pre-VIA output, cool-web) → E4 +VIA blink → E5 +HD44780;
  RAM/stack/JSR; the crystal upgrade beat ("the clock was the bug" —
  Booth) → E5.5 CA1 IRQ button → E6 monitor: the sixty5o2 ROM as
  shipped payload; the 74C922 keypad encoder (16 keys → 4-bit code +
  DA strobe → CA1, debounce built in) upgrades it to the KIM-1
  experience Booth built. A FAULT LIBRARY falls out of the reports:
  loose RAM wire (subroutine never returns), bit-reversed EEPROM
  image, sampling-edge timing — real builders' real failures, worth
  simulating as teachable faults.
  **NEW PARTS RANKED:** 6532 RIOT + 6507 mask (unlocks the 6507SBC
  tier) > 74C922 keypad encoder > 74HC374/574 write-only latch port >
  16C550 UART (completes KIT1) > 74HC688 comparator (qualified-decode
  teaching; Vectron/cool-web both use it). Parked: PS/2 capture chain,
  MC6847+IDT7132, TMS9918A, AY-3-8910, ST7036 LCD (NOT an HD44780 —
  model it or skip cool-web's LCD stage, no stand-ins). Instruments,
  not parts: address/data LED banks, the clock-source selector.
  Lineage note: 6507SBC continues as Nielsen's 65uino — same
  architecture on a PCB, assess when the tier lands.**
* **The module armada + TWO 8051 dev boards, triaged 2026-08-14
  (owner's parts list + two board photos + compliance PDF).** The PDF
  (CE-paperwork only, no schematic) belongs to the SIMPLER of the two
  boards — a YL-39-class "51 Mini Minimum System" board: STC89C52RC in
  a 40-pin ZIF, 4-digit scanned 7-seg, 8-LED row, ~5 buttons, buzzer,
  pot, USB-B power/serial, ISP header. That one is the FIRST board
  preset: every part on it already exists in the device registry, so
  it is pure board-layout work over the original STC89/STC12 emulation
  — the minimum-system board people actually solder first, as a
  one-click preset. Both boards are modeled from physical facts only
  (vendor code and schematics stay research-corpus). THE SECOND,
  MAXIMAL BOARD: PRECHIN 普中51 A2 — bill of materials now VERIFIED
  from the vendor's own module-legend table (prechin.cn/51/91.html,
  archived with provenance in stc-research/corpus/prechin-a2, LOCAL):
  STC89C52-class ZIF with all IO broken out; 2×4-digit common-anode
  7-seg driven by 74HC245 (segments) + 74HC138 (digit select); 8 LEDs;
  8x8 dot matrix off a 74HC595; 4x4 matrix + 1x4 keys; passive buzzer;
  DS1302; DS18B20 header; AT24C02 I2C EEPROM; ADC/DAC = XPT2046 +
  LM358; IR receiver; 5-wire stepper module; NRF24L01 header; LCD1602
  and LCD12864 headers; CH340C USB-TTL; AMS1117-3.3. The SILICON GAP
  LIST IS CLOSED (2026-08-14, coordinator, bw-board 217e10a): DS1302 +
  DS18B20 (dallas-parts.js — CH powers up halted, the classic trap,
  kept; 1-Wire with real presence timing and Dallas CRC8), AT24C02 on
  a NEW FULL I2C SLAVE ENGINE (i2c-slave.js — drives ACKs and data,
  reusable for MPU-6050/TCS34725 later; page-write commit-on-STOP and
  in-page wrap kept), XPT2046 as the plain ADC the boards wire (null
  bit on the command byte's own falling edge — the fact that makes the
  canonical word>>3 driver arithmetic work). All golden-tested by
  bit-banging the protocols over real pullups/dividers through the MNA
  solver. Remaining for the preset: 74HC138/245 as designer parts
  (bw-parts lane) and the board-preset assembly itself. The per-module PORT MAP is
  in the vendor manual (prechin.cn /danganxiazai/, /fanli/) — marked
  for a later stc-research harvest; pin assignments are facts we may
  read, vendor code stays research-only. It becomes the FLAGSHIP BOARD
  PRESET of the original 8051 lane once that gap list lands — the chip
  we started with, on the boards people actually buy, minimum system
  first, full learning board second.
  TIER 0, ALREADY REGISTERED (verified against the device registry):
  HC-SR04 (ultrasonic), WS2812 (neopixel), LCD1602 (hd44780 +
  char_lcd_i2c), 4x4 keypad, LM7805, AMS1117-class (ld1117v33),
  generic h-bridge, IR pair, 74HC595 (shift_register), stepper, servo,
  PCF8574. TIER 1, PARAMETER/PINOUT VARIANTS: LM7809/LM7812, AMS1117-5,
  LCD2004 (same HD44780, 20x4), TB6612FNG and L9110/HG7881 as
  part-accurate faces over the h-bridge core. TIER 2, EASY + HIGH
  VALUE: KY-040 rotary encoder (quadrature + detents + push — the
  missing human-input dial), SN74HC165 (PISO, completes the '595
  pair), CD74HC4067 16-ch mux, DS1302 (3-wire RTC), DS18B20 (1-Wire),
  HX711 (load-cell ADC, 2-wire clocked), MCP3008 (SPI ADC — the bridge
  from MNA-analog to SPI MCUs), HC-05/HC-06 (behaviorally a UART
  bridge peer + a small AT subset — pairs with the tethering story),
  TCS230/TCS3200/GY-31 (light-to-frequency square wave), and the level
  shifters (8-ch MOSFET + BSS138 I2C module) — near-trivial to model,
  PEDAGOGICALLY LOAD-BEARING: they make 3.3V/5V mixing a designer DRC
  lesson instead of a silent assumption. TIER 3, MEDIUM: LCD12864
  (ST7920), SSD1306 OLED, GY-521/MPU-6050 (I2C register model +
  scripted motion stimulus), TCS34725/GY-33 (I2C color), NRF24L01
  (SPI register model + a VIRTUAL AIR CHANNEL — two simulated boards
  talking is the payoff and nobody else's simulator does it), PS/2
  (coordinator coding now — the Vectron/KiT capture chain). TIER 4,
  PARKED AS THE AUDIO LEG: INMP441, ICS-43434, PCM1802 — I2S needs
  infrastructure only the Pico's PIO emulation could honestly carry;
  one design note, no models, until that lane opens. Tiers 1-2 are a
  natural bw-parts follow-on after the retro DIPs; the A2 board preset
  wants DS1302 + DS18B20 + 7-seg scan first. All clean-room from
  datasheets, golden-tested against expectations, cross-checked only
  if a golden disagrees — the owner's stated procedure.**
* **treideme/stc89c52-demos — the APACHE-LICENSED 8051 board corpus
  (owner's find, 2026-08-14).** Apache-2.0 with NOTICE, per-file
  headers, SDCC + meson, targeting the HC6800-ES learning board (same
  class as the PRECHIN A2; the repo carries the HC6800-ES schematic
  PDF — wiring facts freely extractable; the PDF itself is the board
  vendor's, do not redistribute). Sixteen demos that map ONE-TO-ONE
  onto today's device models: DS18B20 on P3.7 (1-Wire, SKIP ROM +
  CONVERT + READ SCRATCHPAD), AT24C02 on P2.0/P2.1 (bit-banged I2C),
  HD44780, ST7920 text AND graphics (the LCD12864 tier-3 reference
  firmware, permissive!), 74HC595, LED matrix, dynamic 7-seg with
  digit select on P2.2-P2.4, buttons on P3.2/P3.3, IR. THE CROSS-CHECK
  THE PROCEDURE CALLS FOR: build these with SDCC (our own toolchain),
  run them on the STC89 emulation with the new devices wired per the
  schematic, and the goldens meet real third-party firmware —
  shippable as examples with attribution. Also seeds an HC6800-ES
  board preset with an exact schematic-grade port map. Fleet lane
  briefed to bw-board (RIOT lane complete).**
* **ELEGOO UNO kit triage (2026-08-14, owner's local kit).** The
  33-lesson canon audited against the registry: ~24 lessons already
  covered. DEVICE GAPS, ranked: DHT11 (single-wire timed protocol,
  the classic), analog joystick (two pots + button, trivial), DS3231
  (I2C, slave engine ready), MAX7219 (serial driver for the existing
  matrix8x8), water-level + sound-module (trivial analog faces),
  MPU-6050 (tier-3, engine ready), RC522 RFID (big SPI model,
  parked), QMI-8658 (rare, parked). LIBRARY LICENSES from the zips:
  LedControl MIT, MFRC522 Unlicense; IRremote/Keypad/Servo/Stepper
  LGPL (service-side compile only, fine); the kit's DHT, DS3231 and
  MPU6050 are OLD GPLv3 COPIES — their upstreams (Adafruit DHT,
  RTClib, i2cdevlib) are MIT today, so the DRIVER-VALIDATION CORPUS
  USES UPSTREAM, never the kit zips; ELEGOO's own lesson sketches
  carry no license → research-only. Kit lessons double as the
  Arduino-drivers lane's stage-3 test list.**
* **MakeCode-extension survey (owner's Calliope list, 2026-08-14).**
  The ecosystem's extension list read as a PRIORITY SIGNAL. (1)
  CONFIRMATIONS — servo, neopixel, ky-040, dht11 exist as of today;
  ssd1306 + tcs34725 are in bw-board's active lane; the MIT pxt-*
  repos join the read-and-compare oracle set for those models. (2)
  NEW I2C GAPS the list ranks for us (all on the slave engine, all
  world-facing params): PCA9685 16-ch PWM/servo driver (the most
  popular robotics part not yet modeled), SCD30/SCD40 CO2 (pairs
  directly with the environment-parameter story), MPR121 12-ch touch,
  HM3301 dust, SI1145 UV/light. (3) FEATURE ADOPTIONS, the real
  finds: DATALOGGER blocks — log value X as series Y from any
  language into the app's timeline/CSV (the trace-CSV lib is the
  backend; a palette block is the front) — the strongest teaching
  feature in their list; RADIO blocks — confirms the two-sim
  radio/virtual-air-channel priority; TURTLE-ON-DISPLAY — a LOGO
  turtle over the framebuffer displays (ssd1306/st7920) as a
  drawing-blocks family; faces/animation packs as example content.
  (4) SCRIPTED VISION SENSOR idea from huskylens: a camera whose
  detections are world-facing params (user scripts what it "sees") —
  behaviorally trivial, pedagogically rich, park with the long tail.
  BLE/IoT-upload/kit-specific boards: parked.**
* **Snap-together function-block kits — ADJUDICATED 2026-08-14
  (owner's question; NICE-TO-HAVE tier).** The kit brand is
  trademarked: describe, never name, in anything committed. Manual
  schematics are research-only; block BEHAVIOR is unprotectable
  function; the mechanical snap connector's patents are irrelevant
  (we do not simulate connectors). THE METHOD: never open their
  black-box ICs — model the DOCUMENTED GENERIC EQUIVALENT instead:
  music IC → UM66T (real datasheet; OUR OWN or public-domain tunes,
  never theirs), alarm/space-sound ICs → behavioral sweep/noise
  oscillators (KD9561-class parts have datasheets; synthesis is the
  TCS3200 wave-generator pattern driving the speaker part, audio
  rendered from the buzzer edge stream), recording IC → ISD1820
  (real Nuvoton-lineage datasheet: REC/PLAYE/PLAYL), FM block →
  parked, no honest sim. Passives (reed, lamp, motor, speaker) all
  exist as of e9c66a5. SECOND IDEA WORTH KEEPING: the kits' chunky
  module granularity as a BEGINNER BLOCK TIER — function-block faces
  over existing models in the designer.**
* **Controller panel — DESIGNED 2026-08-14 (owner's Mindstorms-app
  reference), lane briefed to bw-blocks.** A composable input surface:
  place named widgets — joystick (x/y -100..100), D-pad, momentary/
  toggle buttons, sliders (range), dial — in an edit mode, use them in
  a play mode; persisted with the project. EVERY WIDGET BINDS TWO
  WAYS: (1) PROGRAM-FACING, the Mindstorms mode — a Controller palette
  extension (reporters controller [joy1] x, hats when [btnA] pressed)
  plus the same names in generated Py/JS via RUNTIME_EXTENSIONS;
  (2) WORLD-FACING — bound to a PART PARAMETER (slider → pot position
  or DHT11 temperature, joystick widget → joystick part x/y, button →
  keypad key), which works for EVERY target including compiled C on
  the emulated MCU, because firmware reads real simulated hardware.
  The panel is a stage-view mode beside circuit/debugger; widget
  values flow through board.setControl — the interactive face of the
  environment-stimulus blocks.**
* **Hardware steering per language — SETTLED 2026-08-14 (owner's
  question): no interpreter is ever forked.** Three mechanisms by
  target class: (1) COMPILED targets (C on 8051/AVR/6502/Pico):
  PIN/PART declarations lower to register code in generateC — done.
  (2) RUNTIME targets (generated Python/JS): the RUNTIME_EXTENSIONS
  registry — the same drivers the blocks use, including stc12live for
  tethered real/emulated hardware (settled 2026-08-08, zero emitter
  changes); plus the MicroPython flavor, where the board's own API IS
  the language (display.scroll, pin0.write_digital). (3) MACHINE
  targets (BBC/6502 BASIC on the retro machines): memory-mapped I/O
  IS the API — BBC BASIC's ?& indirection and POKE/PEEK reach the
  VIA/ACIA registers, generateBASIC already lowers pin blocks to
  exactly that, and it is period-authentic pedagogy. Convenience
  comes as BASIC PROC/FN LIBRARIES shipped as plain source (our own,
  clean) — libraries in the language, never patches to the verbatim
  zlib interpreters. Environment-stimulus blocks (the long-tail
  entry) are the complement: those steer the WORLD, this steers the
  MCU.**
* **Sensor long tail — NICE-TO-HAVE, not priority (owner 2026-08-14;
  37-in-1 kit TA0018 + named modules).** REQUIREMENT THAT SHAPES ALL
  OF IT: environment parameters (gas ppm, pressure, magnetic field,
  light, pulse rate...) must be settable BOTH in Circuit Designer
  (per-part controls — the existing params/setControl pattern) AND
  from the BLOCKS EXTENSION — a generic "set [parameter] of [part] to
  (value)" stimulus block wired to board.setControl, so a program can
  script its own test environment. Triage: (a) 37-in-1 SMALL FACES —
  DONE (bw-board e9c66a5, ten models in one idiom: analog/digital
  hall, reed, TTP223, IR reflect pair, photo interrupter, flame,
  sound, heartbeat waveform, 7-color LED; all world-facing params). (b) I2C parts ON THE SLAVE ENGINE: TCS34725,
  BMP180/GY-68, BMP280, MS5611/GY-63, AGS02MA TVOC, SSD1306 OLED
  (framebuffer like ST7920's). (c) UART-FRAME DEVICES: DFPlayer Mini
  (command set + now-playing state; audible audio is app-level),
  ZE08-CH2O (periodic frame emitter) — both pair with the HC-05
  serial-peer architecture. (d) NAMED FACES over existing models:
  MQ-2..-9/-135 over gas_sensor with per-gas curves, LJ12A3-4-Z/BX
  inductive proximity as a param switch, CH340G as the tethering
  story it already is. Kit PDF/instructables text: research-only.**
* **micro:bit lane — SPIKED 2026-08-14, brief ready for the next free
  session.** The official V2 simulator re-verified: MIT (SPDX headers),
  WASM MicroPython with the full board (5x5 display, buttons, touch
  pins, light/sound sensors, accelerometer, RADIO), embeddable iframe
  with a documented postMessage protocol — host sends flash (a
  filesystem object!), stop/reset, serial_input, sensor_set; receives
  ready, request_flash, serial_output, radio_output. PLAN: (1)
  generateMicroPython flavor in sb3-creator — generatePython exists,
  the flavor maps blocks to the microbit API — with the VERB SPLIT: say = stage (degrades by name on the board), display = LEDs, print = serial
  makes this the FIRST target where the say block means something
  physical; (2) SELF-HOST the simulator build (MIT permits; sync
  script like emu8051-wasm, no external hosts); (3) app: DEVICE
  MICROBIT routes the right pane to the sim iframe, run = flash
  message, serial into our terminal, referee stays the logic oracle;
  (4) LATER: radio_output/radio_input between TWO sim iframes = two
  boards talking — pairs with the NRF24L01 virtual-air-channel idea.
  (1) IS DONE (coordinator, sb3-creator b75f129):
  generateMicroPython with generators AS the scheduler (yield ms at
  waits, 0 at back-edges, round-robin driver on running_time(),
  broadcasts spawning receiver tasks), display → display.scroll / print → serial (say degrades: stage semantics), key a/b
  → buttons, play-note → music.pitch, named degradations for
  everything else — and every emitted program must parse under
  python3 compile(), gallery sweep included. REMAINING: (2) self-host
  the sim, (3) app wiring (flash/serial postMessage bridge) — the
  next free app-capable session.**
* **Tier-2 status (2026-08-14 end of day): CLOSED except HC-05.**
  Landed golden-tested on bw-board master: DS1302, DS18B20, AT24C02
  (+ the reusable I2C slave engine), XPT2046, KY-040, 74HC165,
  74HC138, 74HC245, ST7920, MCP3008, HX711, TCS3200, CD74HC4067,
  level_shifter4 (initiator-tracked bidirectional — highs never cross
  domains, proven against a real LDO rail). Remaining: HC-05 as a
  serial peer (stimulus-level, pairs with the telemetry panel work),
  regulator parameter variants (trivial), and the sidecar artwork in
  bw-parts' running lane.**
* **Arduino-drivers TO-DOs (2026-08-14):** the goal is stock Arduino
  library sketches running unmodified on the emulated AVR against our
  device models. (1) Bit-banged libraries (OneWire+DallasTemperature,
  LiquidCrystal, U8g2 in SW-SPI mode) need nothing new — they speak
  the pin-level protocols our models already decode; add their
  canonical example sketches to the compile-service library set and
  the oracle corpus. (2) Hardware Wire/SPI sketches need the
  avr8js-adapter to instantiate its TWI/SPI peripherals for the
  ATmega328P and bridge them to devices at TRANSACTION level — the
  i2c-slave engine's onAddress/onWriteByte/onReadByte handler seam is
  the shared face; factor device handlers so edge-level and
  transaction-level buses drive the same object. (3) MIT/BSD driver
  repos (LCD12864 etc. searches) are TIEBREAKER ORACLES only —
  consulted read-and-compare when a golden disputes; clean-room from
  datasheets stays the build procedure. Lane queued to bw-board
  behind the 8051 corpus.**
* **Telemetry TO-DOs (2026-08-14; background in
  stc-research/serial-dashboard-survey.md, LOCAL):** (1) live
  telemetry panel — feed the instruments-panel scope widgets from the
  tethered serial stream; the dialect's print output carries a fixed
  one-line frame convention of our own. (2) CSV export — DONE for the
  debugger's execution trace (brickwright-lite b35ba4d, traceToCsv +
  drawer button); scope/instrument channels can reuse the same lib. (3) serial ports open WITHOUT
  toggling DTR/RTS and restore line state on close (CH340C boards
  power-glitch otherwise; live-monitor.py fixed 4ec9849, stc12live
  driver must match; verify on the A2 at the next bench session).**
* **labwired-core — FOUND 2026-08-13, evaluation seeded.** An MIT, Rust,
  in-repo simulation engine (no open-core split: the hosted playground
  "runs the same models") covering Cortex-M0+/M3/M4/M7/M33, RISC-V and
  Xtensa, with modeled boards including nRF52840/nRF54L15, STM32s,
  RP2040 and ESP32-C3 paths — and, remarkably, OUR OWN fidelity
  doctrine: a ledger classifying every model as Modeled / Smoke-tested /
  Hardware-compared, with silicon-diff validation reports. Three
  consequences, in order of certainty:
  1. **RP2040 second executor** (immediate): an independent MIT
     implementation for layer 5 of the oracle pyramid — run our compiled
     Pico artifacts under its CLI, emit the canonical trace, diff against
     rp2040js and the referee. Evaluation seeded to the oracle lane.
  2. **nRF52-class reopens**: at minimum a CI oracle for micro:bit v2 /
     Calliope-class boards; since the engine demonstrably compiles for
     their browser playground, an in-browser nRF52 path stops being
     unthinkable — pending a read of their per-board validation matrix.
  3. **The ESP32 assessment softens at the edges**: their ESP32-C3
     (RISC-V) path in an MIT engine may be the "permissive peripheral
     model emerging" that our deferral said to watch for. Xtensa depth
     unverified; the validation matrix decides, not the README.
* **Deferred:** STM32 (Renode, MIT, covers it as a CI oracle; a browser
  runner could reuse rp2040js's MIT Cortex-M0 core with our own peripheral
  layer — possible, big, not scheduled), micro:bit (behavioral simulator,
  different fidelity class).
* **Fidelity is declared, not discovered:** `avr8js` covers timers/UART/GPIO/
  ADC well; `rp2040js` is solid on GPIO/timer/UART, thinner on PIO. Each
  target gets a per-peripheral capability row in DEBUG-CONTROL-MODEL's
  matrix — the three 8051 targets were never equal either, and that design
  now pays for itself.

**The `DEVICE` axis in the pseudocode already carries part-awareness
(STC12C5A60S2 / STC89C52RC / STC15F2K60S2); `DEVICE ARDUINO_NANO` and
`DEVICE PI_PICO` extend an existing axis, not a new design.** The millisecond
scheduler tick is target-portable by construction.

**STATUS 2026-08-13 — the phase is essentially COMPLETE.** All three
architectures run end-to-end IN THE PRODUCTION APP, proven by the
five-assertion user-visible probe (lite `scripts/proof-production.mjs`:
live position, blinking bench LED, visible serial, honest pause, working
step — 5/5 on both Nano and Pico). The block surface is complete on all
cores: digital in/out, ADC, PWM dimming, servo, motor, serial print, the
cooperative scheduler, and full boundary-D debugging. Examples retarget
mechanically (`retargetPseudocode` + per-device role pools; gallery
device lists are COMPUTED from dry-runs and test-enforced). Remaining in
flight: the app's "switch device" affordance, the pull-down PinMode
(mna-gated), the simavr differential oracle — and the bench session that
puts the first chain on real silicon.

**Upload & tethering (the bench era's other half) — surveyed 2026-08-13.**
How the micro:bit/Calliope world ships programs, and what we adopt:

* The micro:bit's editors compile IN THE BROWSER (a typed-blocks language
  compiled to native ARM against a precompiled MIT runtime blob — CODAL),
  emit a universal .hex, and upload three ways: (a) drag-drop onto the
  DAPLink mass-storage drive — the friendliest flash path ever shipped;
  (b) WebUSB CMSIS-DAP direct flashing from the editor, with PARTIAL
  flashing (only the changed program region) for speed; (c) BLE partial
  flashing. Direct control runs through a resident firmware exposing
  sensors/actuators over BLE/serial to the browser — the same
  architecture as our stc12live tethered mode.
* The Calliope mini is the same DAPLink lineage; the Fraunhofer block
  environment compiles SERVER-SIDE (their NEPO blocks → C++ → .hex, our
  own hosted-compiler pattern), then drag-drop or a small watcher app
  copies the hex; the mini 3 added one-click WebUSB.
* What we adopt, per device, when the bench era opens:
  - Pico: UF2 mass-storage drag-drop (the service grows elf2uf2 output —
    the format IS the friendliness) + WebSerial picotool later.
  - AVR Nano/Uno: WebSerial STK500v1 in the browser — permissive JS
    implementations exist; no native helper needed.
  - STC12/15: a WebSerial port of the stcgal protocol (the cold-boot
    handshake is the only hard part, and we know that protocol cold).
  - 6502: an EEPROM programmer story (out-of-browser initially; document
    the TL866/Arduino-programmer paths in the getting-started docs).
  - Tethering everywhere: resident firmware + WebSerial is our
    generalization of their model, without a helper app.
* micro:bit — REASSESSED 2026-08-13, now a REAL candidate (owner found
  the key): the Foundation's official V2 simulator is MIT — MicroPython
  compiled to WASM with CODAL simulated in JS, full V2 board peripherals
  (5x5 display, buttons A/B, accelerometer, light, microphone, touch
  pins, radio), embeddable via iframe + postMessage (flash a script,
  script sensor values, serial both ways). Our path is unusually short:
  DEVICE MICROBIT already parses (core micropython, P0-P20 + buttons),
  generatePython exists and needs a microbit FLAVOR (from microbit
  import *; the cooperative multi-WHEN structure becomes a polled main
  loop); the app embeds the sim in an iframe and drives it over
  postMessage. Capability row, stated honestly: behavioral simulation
  with on-board peripherals and serial - NO electrical bench (its pins
  are not our MNA nets), NO boundary-D debugger (no position protocol
  in MicroPython) - run + watch + print, which for this board is the
  authentic experience. Real-hardware upload rides microbit-fs (MIT,
  universal hex with the script appended) and WebUSB DAPLink - both
  permissive, both browser-native. Self-hosting a pinned build is
  MIT-clean; the Foundation asks to be told, so we tell them.
  Additionally the nRF52840 model in the MIT engine below is
  silicon-verified, so a REGISTER-level micro:bit-class path (CODAL
  binaries under emulation) stops being unthinkable - behavioral first,
  it ships this decade.
  Calliope: NOT covered by the micro:bit sim (different board); its
  MicroPython story could ride the same flavor later - separate
  assessment when asked for.

**Division of labour:** coordinator writes the two contract-bearing pieces
(the avr8js boundary-A adapter and the debugger port); the fleet takes the
avr-gcc endpoint, board sidecars with datasheet-audited pin tables (the pin
chooser needs per-pin meanings for D0–D13/A0–A5 exactly as it has for the
STC12), device selection in the app, and an examples wave per device.

## Examples gallery: generic circuits, explicit targets (2026-08-12)

The gallery must grow faster than any one MCU port. Its metadata and UI should
therefore separate three axes that are currently too easy to conflate:

1. **Concept and level.** Every example gets a stable concept/category and a
   difficulty level (Beginner, Intermediate, Advanced). Pure-circuit lessons
   are first-class examples, not a special screen or an afterthought.
2. **Parts involved.** Entries advertise capabilities such as `no-mcu`, `mcu`,
   `resistor`, `led`, `diode`, `transistor`, `motor`, `sensor`, and `display`.
   The browser uses these tags for filters, so “MCU / non-MCU” is only the
   first useful question; the same mechanism can expose any part family later.
3. **Execution target.** A program may name a concrete target (`stc12`,
   `stc89`, `arduino-uno`, `arduino-nano`, `pico`) or declare a generic target
   contract. Target filters must be data-driven and open-ended, not a hard-coded
   STC-versus-everything-else switch.

The preferred authoring model is **generic-first**. A lesson about blinking,
debouncing, PWM, ADC thresholds, or cooperative tasks should describe the
portable pins, capabilities, timing assumptions, and expected behavior once.
Target adapters then map that contract to STC12, AVR/Arduino, RP2040, and later
chips. When a physical detail genuinely differs — pin names, voltage, timer
resolution, or peripheral availability — keep a small target-specific variant
beside the generic lesson and state the difference. Do not silently rewrite an
STC12 lesson as an RP2040 lesson: that would make the circuit and the learning
objective ambiguous.

Near-term implementation order:

* add explicit `parts`, `targets`, and optional `capabilities` metadata to the
  gallery index, while keeping the current `kind` field for compatibility;
* make the Examples panel open by default, readable in dark themes, scrollable,
  and filterable by category, level, parts, and target family;
* tag the existing pure-circuit wave first, then add generic MCU lessons and
  target projections for STC12, AVR/Arduino, and RP2040;
* validate each projection against the same circuit JSON, expected behavior,
  simulator capabilities, and compiler/debugger contract.

This keeps the teaching content reusable without pretending that all MCUs are
electrically interchangeable. The gallery is a catalog of concepts plus
explicit portability evidence, not merely a list of firmware files.
