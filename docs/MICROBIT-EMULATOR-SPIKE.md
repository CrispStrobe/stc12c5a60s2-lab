# micro:bit emulator spike — can we single-step compiled Cortex-M4 firmware in JS?

**Spike, not a build.** Decides Path B in [`MICROBIT-NATIVE.md`](MICROBIT-NATIVE.md)
§5 Stage 3: a native micro:bit debugger needs an nRF52833 / Cortex-M4 emulator
that owns a program clock and steps instructions, running the *compiled*
firmware — the `rp2040js`/`avr8js` pattern, extended to M4. This spike actually
ran the code. Findings are backed by register dumps, not by "the package
exists."

Done 2026-08-19. Work lives in the scratchpad
(`…/scratchpad/spike/` — `step_test.mjs`, `fw_test.mjs`, `mp_test.mjs`,
`hook_test.mjs`). **Not committed** — for review.

---

## 1. Verdict: **FEASIBLE (with caveats)** — via Unicorn.js

**The CPU-core half is proven, in-browser-viable, today.** I single-stepped real
Thumb-2 machine code (16-bit *and* 32-bit-wide instructions), read registers
after each step, loaded a compiled Cortex-M4 image via its vector table, and
loaded + stepped **the real MicroPython micro:bit V2 `.hex`** — all in JS/WASM.

**Emulator: [`@alexaltea/unicorn-js`](https://www.npmjs.com/package/@alexaltea/unicorn-js)** — a
port of the Unicorn 2.1 CPU emulator (itself QEMU-derived) to JS/WASM via
Emscripten.

| axis | finding |
|---|---|
| **npm / version** | `@alexaltea/unicorn-js@2.1.4`, published 2026-06-19; single-arch ARM bundle `@alexaltea/unicorn-js/arm` (~817 KB, WASM embedded) |
| **License** | **GPLv2** (inherited from Unicorn). ⚠️ **The one hard caveat** — see §5 |
| **Browser / WASM** | **Yes.** Emscripten build, detects `globalThis.window`; requires WebAssembly + BigInt (universal in modern browsers). Loadable over CDN as one file. Node also works (I tested under Node 26) |
| **Cortex-M4 / Thumb-2** | **Yes.** `MODE_THUMB \| MODE_MCLASS` selects M-profile; full ARMv7E-M Thumb-2 incl. wide instructions and M-profile system registers decode correctly |
| **Single-step** | **Yes.** `emu_start(begin, until, timeout, count=1)` steps one instruction; also `HOOK_CODE` fires per-instruction |
| **Maturity** | Unicorn is mature; the JS port is actively published. Caveats below (IT-block corruption under count-limit; no `uc_ctl` binding) are known and worked-around |

**Why "with caveats" and not a clean yes:** (a) **GPLv2** collides with the
repo's MIT/MPL norm — a genuine strategic constraint, §5; (b) the **peripheral
models are the bulk of the work** and none exist for nRF52833 (§3); (c) for the
*MicroPython* `.hex`, instruction-stepping shows **VM internals, not the user's
Python** — which reframes what the debugger is *for* (§5, the decisive finding).

**Runners-up** (evaluated, not chosen):

- **InfiniEmu** (`pipe01/infiniemu`, **GPL-3.0**) — HN item 40846943. A *full
  nRF528**32*** SoC in C→WASM, runs PineTime InfiniTime firmware in a browser
  tab. Real peripherals already modeled (I²C, SPI, display). **But:** wrong chip
  (nRF52832 ≠ nRF52833 — same M4 core, different peripheral map/memory sizes),
  no step/GDB API exposed, author says "not production ready." Best *if* you want
  peripherals for free and will retarget + add stepping. GPL-3.0 is worse for us
  than GPLv2.
- **rp2040js** (`wokwi/rp2040js`, **MIT**, pure TS) — the reference pattern, but
  **Cortex-M0+ only**. Extending its hand-written decoder to M4 is **LARGE, not
  incremental**: M4 adds the full 32-bit Thumb-2 set (hundreds of encodings),
  IT-blocks, DSP/SIMD, and an **FPU** (nRF52833 is Cortex-M4**F**). rp2040js
  decodes only the ~6 wide ops ARMv6-M mandates and has no FPU. Verdict: **do not
  extend rp2040js's core** — mine it for peripheral-modeling *patterns* instead.
- **zmu** (`jjkt/zmu`, Rust) — has proper GDB single-stepping across M0–M7/M4F,
  but no nRF52 device model and **no proven WASM/browser build**. Unknown.
- **ktock/qemu-wasm** — QEMU-in-browser, GPLv2, research-grade, heavyweight.
- **The micro:bit "simulators" are a trap.** `micropython-microbit-v2-simulator`
  (MIT, the one we ship) and MakeCode's sim recompile *source* to WASM/JS — they
  are **not CPU emulators**, there is no ARM instruction stream to step. Ruled
  out for this purpose. This matches the Stage-3 correction already in
  MICROBIT-NATIVE.md.

---

## 2. What I proved (the actual runs)

### Proof A — CPU core: single-step Thumb-2, read registers (PASS)

Assembled with `arm-none-eabi-as -mcpu=cortex-m4 -mthumb` so encodings are
ground truth: a 16-bit loop summing 10+9+…+1, then two **32-bit-wide Thumb-2**
instructions (`movw`/`movt`) building `r2 = 0x56781234`.

```
;  200a        movs r0,#10
;  2100        movs r1,#0
;  1809  loop: adds r1,r1,r0
;  3801        subs r0,#1
;  d1fc        bne  loop
;  f241 2234   movw r2,#0x1234   <- 32-bit wide
;  f2c5 6278   movt r2,#0x5678   <- 32-bit wide
```

Stepping in Unicorn (`ARCH_ARM`, `MODE_THUMB`), reading PC/r0/r1/r2 after each
`emu_start(pc|1, end, 0, 1)`:

```
init    PC=0x00010000 r0=  0 r1= 0 r2=0x00000000
step 1  PC=0x00010002 r0= 10 r1= 0 ...          <- movs
...loop runs 10×, r1 accumulates...
step 31 PC=0x00010008 r0=  0 r1=55 ...          <- loop exits
step 33 PC=0x0001000e r0=  0 r1=55 r2=0x00001234  <- movw (PC +4: a WIDE insn)
step 34 PC=0x00010012 r0=  0 r1=55 r2=0x56781234  <- movt (PC +4)
```

**The smoking gun for Thumb-2:** steps 33–34 advance PC by **4 bytes**, and r2
lands exactly on `0x56781234`. Final registers match the hand-computed expected
values (`r0=0, r1=55, r2=0x56781234`) exactly. Instruction-level stepping +
register read work in JS/WASM. **This alone establishes the CPU-core half.**

### Proof B — vector-table load path + run-to-completion (PASS)

Compiled a bare-metal nRF52833 image (`arm-none-eabi-gcc … -T fw.ld`) with a
real vector table (SP@0x0, reset@0x4) and a reset handler that counts to 10000
and stores the result to RAM. Loaded it exactly as a `DebugTarget` would:

```
Vector table read from flash:  initial SP = 0x20020000   reset PC = 0x00000008
reset   PC=0x00000008 SP=0x20020000 ...
step 1  PC=0x0000000a  r0=0x20000100    <- ldr r0,=literal (literal-pool load works)
step 3  PC=0x00000010  r2=10000          <- movw wide
run-to-store (100k-insn budget):
halted  PC=0x00000018  r1=10000 r2=10000
RAM[0x20000100] = 10000  PASS
```

This proves the whole load path: **read initial SP from flash[0], PC from
flash[4], strip the Thumb bit, map flash + 128 KB RAM, step the reset handler,
run under an instruction budget, and read the memory it wrote back.** The bounded
`emu_start(pc, until, 0, budget)` **is** `runFor` — Unicorn owns its clock, which
is exactly the `DebugTarget` contract `debug-session.js` requires (`runFor` in
program time, `onHalt` on completion).

### Proof C — the REAL MicroPython micro:bit V2 firmware loads and steps (PASS)

Downloaded `micropython-microbit-v2.1.1.hex` (the official release, 1.2 MB Intel
HEX — *plain* Intel HEX, not universal-hex, since this is the V2-only release).
Parsed it (450 KB into flash), read its vector table, and single-stepped the
**actual** reset handler:

```
vector table: initial SP=0x20000400  reset PC=0x00000a80
step  1  PC=0x00000a82 ...
step  2  PC=0x00000416 ...     <- into the C-runtime startup
...45 real instructions of .data/.bss init + reset path...
step 46: FAULT  ERR_INSN_INVALID (10) at PC=0x00000a8a
         instr bytes = 81 f3 08 88  =  MSR MSP, r1
```

**The fault was diagnostic, not a dead end.** `MSR MSP` is an **M-profile
system-register write** — rejected because I had created the engine with
`MODE_THUMB` alone. Re-created with **`MODE_THUMB | MODE_MCLASS`**, the same
firmware:

```
step 381  PC=0x00000a8a  <- the MSR MSP that used to fault...
step 382  PC=0x00000a8e  <- ...now executes cleanly
...runs 3,000,000 instructions with no fault...
```

**A real, unmodified micro:bit V2 firmware image loads at the right base, its
vector table drives the initial SP/PC, and hundreds of thousands of real
instructions step correctly in JS/WASM.** Within the 3M-instruction budget it
never touches an unmapped peripheral — it spins in early startup (flash+RAM
only), consistent with a SoftDevice/MBR-style handoff that needs more machine
state to progress. Fully *booting* it needs the peripheral models in §3; *loading
and stepping* it is done.

### Proof D — hooks: breakpoints, watchpoints, and the peripheral mechanism (PASS)

```
HOOK_CODE fired count = 33   first PCs: 0x10000,0x10002,0x10004,0x10006,0x10008
mem-range hook added OK over 0x40000000..0x40010000
```

- **`HOOK_CODE`** fires once per instruction (33 = 2 + 10×3 + 1, exact) with the
  right PCs → the mechanism for **breakpoints** and instruction counting.
- **`HOOK_MEM_READ|WRITE` over a range** installs cleanly → **watchpoints**, and
  crucially **the way you model nRF52833 peripherals**: trap MMIO reads/writes to
  `0x40000000+` and return synthetic register values (e.g. answer
  `EVENTS_HFCLKSTARTED = 1` immediately) *without* mapping real memory there. Full
  hook palette present: `INTR, INSN, CODE, BLOCK, MEM_{READ,WRITE,FETCH}[_UNMAPPED],
  INSN_INVALID`.

### Key API findings (for whoever builds this)

- **`MODE_MCLASS` is mandatory.** `MODE_THUMB` alone faults `ERR_INSN_INVALID` on
  the first `MSR MSP`/`MRS`/`CPS`. Create the engine as
  `new uc.Unicorn(uc.ARCH_ARM, uc.MODE_THUMB | uc.MODE_MCLASS)`.
- **Start address must be odd** (Thumb bit) — pass `pc | 1` to `emu_start`; but
  write the **PC register** with the Thumb bit *stripped* (`& ~1`).
- **No `uc_ctl` / `cpu_model` binding is exposed** in this JS build, so you can't
  request `"cortex-m4"` by name — `MODE_MCLASS` is the only M-profile lever. Good
  enough (M-profile Thumb-2 + system regs work); FPU status is the open question
  (§5).
- **`count=1` corrupts IT-blocks** (upstream Unicorn #853): stepping one
  instruction through an `it`/`ite` executes the conditional op regardless of the
  condition, because the count-limit tears the IT sequence. For a correct
  single-step debugger, **prefer a `HOOK_CODE` that stops after one instruction
  over `count=1`** at IT boundaries. Real M4 code is full of IT-blocks, so this
  matters.

---

## 3. Peripheral-model scope — the bulk of Path B

A typical micro:bit program (show something, read a button, read the
accelerometer, `print`) touches the peripherals below. Effort tier = work to
model it well enough to get **past reset and run a simple program** (not
bit-exact silicon). The mechanism is always the same nRF idiom — a write to a
`TASKS_*` register triggers an action; you set the matching `EVENTS_*` register
and (if enabled) raise the NVIC line — implemented over Proof-D's MMIO hooks or a
`mem_map`'d region.

Confirmed micro:bit V2 pin map (tech.microbit.org): LED rows P0.21/22/15/24/19,
cols P0.28/P0.11/P0.31/P1.05/P0.30; **Button A P0.14, Button B P0.23**; internal
I²C **SCL P0.08 / SDA P0.16**, sensor INT P0.25; UART target TX P0.06 / RX P1.08
@ 115200.

| peripheral | what / how firmware drives it | tier |
|---|---|---|
| **Core + vector table + NVIC/SCB** | SP@0/PC@4 (proven), exception entry/return stacking, interrupt controller | **built-in** to Unicorn; must wire IRQ delivery |
| **CLOCK** (HFCLK/LFCLK) | write `TASKS_HFCLKSTART`, **startup spins on `EVENTS_HFCLKSTARTED`** — set it immediately or boot hangs | **SMALL** but *mandatory first* |
| **NVMC** (flash ctrl) | poll `READY`; reads just need flash mapped (done) | **SMALL** read-only; MEDIUM if the program writes flash |
| **GPIO P0/P1** | `OUT/OUTSET/OUTCLR/DIR/IN`; drives LED matrix + reads buttons | **SMALL–MEDIUM** (two 32-bit ports) |
| **GPIOTE** | per-channel edge → `EVENTS_IN[n]` / PORT event → NVIC; buttons | **MEDIUM** |
| **TIMER0–4** | 16 MHz up-counter + CC compare → event/IRQ; CODAL's system tick + display refresh + touch | **MEDIUM** (needed to make time advance) |
| **RTC0–2** | 24-bit LFCLK counter; SoftDevice/BLE + low-power timing | **MEDIUM** (only if firmware waits on it) |
| **5×5 LED matrix** | *not a peripheral* — GPIO row/col multiplex scan in the timer ISR; brightness = per-row duty. Model = sample GPIO OUT per scan | **MEDIUM** (logic is in firmware; you reconstruct the frame) |
| **Buttons A/B** | GPIO in P0.14/P0.23 + GPIOTE/PORT edges; debounced in ISR | **SMALL** once GPIO+GPIOTE exist |
| **TWIM (I²C accel/mag)** | EasyDMA master: `PSEL.SCL/SDA`, `TXD/RXD.PTR/MAXCNT`, `TASKS_STARTTX/RX`, poll `EVENTS_*`. **Needs a modeled LSM303AGR** (accel 0x19 / mag 0x1E, WHO_AM_I etc.) or CODAL's sensor probe hangs | **LARGE** (DMA state machine **+** a sensor device model — often the real blocker to a full CODAL boot) |
| **UART/UARTE** | UARTE = EasyDMA serial (`PSEL`, `BAUDRATE`, `TXD.PTR/MAXCNT`, `TASKS_STARTTX`, `EVENTS_ENDTX`) @115200; the `print` path | **MEDIUM** (LARGE for full DMA; SMALL if you support legacy byte-at-a-time UART) |
| **RADIO (2.4 GHz/BLE)** | packet DMA + PHY; only if the program uses radio | **LARGE** — stub the registers for v1 so it doesn't hang; model later (BabbleSim is the radio-channel story, separate feature) |

**Minimum set to get a hex past reset and blinking:** core+vectors, CLOCK (fake
instant `HFCLKSTARTED`), NVMC-read, GPIO, one TIMER (system tick), GPIOTE
(buttons), NVIC. To not fault on sensor init you additionally need **TWIM + an
LSM303AGR stub**. RADIO/UARTE can be stubbed first.

**Copy rp2040js's incremental pattern** (MIT, safe to read): one small class per
peripheral with `readUint32(offset)`/`writeUint32(offset,value)` mapped into the
address space + a handle to the interrupt controller; add them one at a time
(GPIO/UART → "blink"+"print", then timers, then DMA blocks), each behind unit
tests. The nRF twist vs RP2040: writes to `TASKS_*` *do* things and you must set
`EVENTS_*` + raise the NVIC line.

---

## 4. Recommended plan — first 2–3 steps to a working `DebugTarget`

The factory (`bw-board/src/debug-target-factory.js`) already dispatches on a
`kind` string and already lists `'rp2040js'` (pure-TS Cortex-M0+). A micro:bit
target is a **new kind wired identically** — nothing in `debug-session.js`
changes; it already branches on `capabilities()` and calls `runFor`/`step`/
`onHalt`.

1. **Stand up `createMicrobitTarget(opts)` over Unicorn.js** (new kind
   `'microbit'` / `'nrf52'`). Construction: load `@alexaltea/unicorn-js/arm`,
   `new uc.Unicorn(ARCH_ARM, MODE_THUMB|MODE_MCLASS)`, `mem_map` flash 0..512K +
   RAM 0x20000000..+128K, `parseIntelHex(opts.hex)` (the app already has
   `intel-hex.js`; my parser confirms the V2 hex is plain Intel HEX), write flash,
   set SP/PC from the vector table. Map the contract:
   - `runFor(budgetNs)` → convert ns→insn count at 64 MHz (mirror the rp2040js
     adapter's ns↔insn mapping) → `emu_start(pc|1, 0, 0, count)`; return
     `'halted'` when a breakpoint hook stops it.
   - `step('insn')` → one-instruction run **via `HOOK_CODE`** (not `count=1`,
     because of the IT-block bug).
   - `capabilities()` → `insn: true, line: via-source-map, breakpoint-addr: true,
     read regs/mem: true` — the M4 column is the *opposite* shape from the
     MicroPython-sim column in MICROBIT-NATIVE.md §2 (this target is register/insn
     rich, Python-frame poor).
   - registers via `reg_read_i32(ARM_REG_*)`, memory via `mem_read/mem_write`.
2. **Model CLOCK first, then GPIO+TIMER+NVIC** (the §3 minimum) over MMIO hooks —
   just enough to carry a compiled blink from reset to a visible LED-matrix scan.
   Prove it with a MakeCode/CODAL blink hex (native ARM — see §5) so the LED
   frame is reconstructable from GPIO.
3. **Add the source map + line stepping.** For a genuinely useful debugger, feed
   it **native-compiled firmware with a source map** (MakeCode/PXT or CODAL),
   translate `insn` steps to source lines, and register `'microbit'` in
   `getTargetKinds()` + the debug panel — which already renders whatever
   `capabilities()` reports.

Sequencing note: steps 1–2 are the multi-session core work; the peripheral models
(§3) are where the weeks go, exactly as MICROBIT-NATIVE.md predicted.

---

## 5. Honest blockers

- **GPLv2 (the biggest one).** `@alexaltea/unicorn-js` is GPLv2; InfiniEmu is
  GPL-3.0. Every other piece in this ecosystem is MIT/MPL. Bundling a GPL
  emulator into the shipped app is a real licensing decision for the owner — it
  is not a blocker to *building/proving* Path B, but it **must be resolved before
  shipping**. Options: (a) accept GPL for this component and isolate it; (b) use
  Unicorn only as an out-of-process/worker oracle; (c) invest in a clean
  MIT M4 core (large). Flag for an explicit owner call, like the MPL decision.
- **FPU/DSP — unverified.** nRF52833 is Cortex-M4**F**. `MODE_MCLASS` gave
  correct integer Thumb-2 + M-profile system registers across 3M instructions,
  but I did **not** exercise a VFP instruction (`vadd.f32`, s0–s31) or a DSP/SIMD
  op. Unicorn/QEMU implements FPv4-SP, but whether this JS build enables it under
  `MODE_MCLASS` (no `uc_ctl` to set the CPU model) is **untested**. Any firmware
  doing floats — and MicroPython/CODAL will — needs this confirmed. **First
  follow-up: assemble a `vadd.f32` and step it.**
- **WASM-vs-Node: clear.** The build detects `window` and runs in browsers over
  WASM+BigInt; I tested under Node, but nothing is Node-only. Not a blocker.
- **hex-vs-ELF: clear.** The V2 MicroPython release is plain Intel HEX (not
  universal-hex), parseable with the app's existing `intel-hex.js`. ELF not
  needed. (Older *combined* V1+V2 downloads are universal-hex — if we ever ingest
  those, add a universal-hex splitter.)
- **The decisive one — MicroPython VM vs native, i.e. *what does "step" show?***
  - **MakeCode/PXT compiles the user's program to native ARM Thumb** (static
    compilation, not bytecode). There, one ARM instruction traces back to a user
    source construct — **`insn`-stepping ≈ source-stepping**, given PXT's source
    map. This is the case where an ARM debugger is genuinely useful to a learner.
  - **The standard micro:bit MicroPython `.hex` is a bytecode interpreter.**
    Emulating the ARM and stepping shows the **VM's fetch-decode-dispatch loop**
    (`mp_execute_bytecode`), **never the user's Python lines**. To debug Python
    you'd have to detect you're inside the VM, decode its bytecode `ip` and
    line-number tables at runtime — a Python-level debugger built *on top of* the
    ARM emulator, a separate and larger project.
  - **Implication:** an instruction-stepping micro:bit debugger is only
    *directly* useful against **native-compiled firmware**. So Path B pulls the
    compiler strategy with it: to get a source-mapped native `.hex`, prefer the
    **MakeCode/PXT or CODAL C++** lowering — which MICROBIT-NATIVE.md §7 currently
    lists as out of scope. This is the strategic coupling to surface to the owner:
    *the emulator is feasible, but its value is maximized by a native compile
    path, not the MicroPython path we ship today.* Debugging the MicroPython VM's
    ARM execution is possible but shows VM internals, not the student's program.

**Bottom line:** the emulator half of Path B is real and proven in JS/WASM today
(Unicorn.js, `MODE_MCLASS`, hooks for breakpoints + peripherals). The work ahead
is the nRF52833 peripheral models (§3, weeks, incremental) and two decisions for
the owner: the **GPL license**, and whether to add a **native compile path** so
instruction-stepping maps to the user's program rather than the VM's.
