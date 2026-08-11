# Verification ledger — what this campaign proved, and what it did not

One row per claim. Four columns: what is claimed, the number, the evidence
category (per `EVIDENCE-CATEGORIES.md`), and what would raise it.

Nothing here has run on real silicon. The bench questions (by ID) name which
rows they would settle.

**On the category labels.** `EVIDENCE-CATEGORIES.md` defines three: **1**
independent-source, **2** same-source (two implementations from one document),
**3** single-implementation. Rows here previously used a `2b` shorthand for
"category 2, and both sides were modified in this campaign"; that sub-label is
used once in the taxonomy document and never defined there. The rows below now
carry the three defined categories, with "both sides modified this campaign"
stated in prose where it applies. Two consequences worth stating plainly,
because both changed rows that looked settled:

- A measurement from **one** emulator is **category 3**, not 2 — category 2
  requires two implementations. Several rows were labelled 2b while naming a
  single source in their own text.
- **emu8051 vs ucsim is category 1**, not 2 — different upstream projects, per
  the taxonomy's own example — carrying the lineage caveat that independent
  upstream is not independent interpretation.

## Cross-model measurements

| Claim | Number | Category | What would raise it |
|-------|--------|----------|---------------------|
| Servo pulse width at 90° | emu8051: **1499.6 µs**, ucsim: **1499.6 µs** (**0.0 µs spread**). Pre-registered prediction: "~1499.6 µs, spread ~0.04 µs". Actual: exact match at measurement resolution. Previously 1500.0 µs with 0.4 µs spread, 90% explained by the SETB/CLR 3 MC defect. Fix (6cb9bc7) earned this convergence. Independent anchor: 1500 µs from FOSC/12 arithmetic. | **1 (lineage)** — two emulators from different upstream projects, matching to <1 µs after an independently found defect was fixed. Caveat as for the corpus sweep: both were modified in this campaign and both read the same datasheet, so independent lineage is not independent interpretation. | **BENCH-PWM** |
| Servo pulse at 0° / 180° | emu8051: **499.0 / 2500.4 µs** (was 499.2 / 2500.6; bias removed by cycle-count fix). ucsim (`356df26`): **499.1 / 2500.0 µs**, frame 20000.0 µs = 50.0 Hz, predictions stated before measuring and matched exactly. Spread **0.1 / 0.4 µs**. 0° at 499 µs is intentional — the driver maps 0–180° to 500–2500 µs (extended-range convention), not a 1 µs error. | **1 (lineage)** — this row previously said "emu8051 only, no ucsim number"; ucsim has since measured all three angles. Same lineage caveat. | **BENCH-PWM** |
| Servo frame period | emu8051: **20003.5 µs** = 50.0 Hz, ucsim: **20000.0 µs**. 3.5 µs spread **unchanged** by the fix — this is ISR dispatch overhead, not the bit-opcode defect (confirmed: the defect contributed 1.45 µs of the old spread, but the frame period is dominated by other timing). | **1 (lineage)** — both emulators measured; remaining spread is ISR overhead, not a known defect. Same lineage caveat. | BENCH-PWM |
| Motor PWM duty (register) | Driver loads **84/128/192** of 256 counts for 33/50/75% | **2** — independent anchor: these are what the driver arithmetic computes | BENCH-PWM |
| Motor PWM duty (pin) | ucsim measured: 33% = **32.83%** pin duty, period **277561 ns** | **3** — ucsim only; a measurement of the pin rather than a re-derivation of the register, which is why it is worth having, but there is no second implementation | BENCH-PWM |
| Motor H-bridge direction decode | Board: IN1=5V/IN2=0V → FORWARD, IN1=0V/IN2=5V → REVERSE | **2** | A motor visibly spinning on silicon |
| Motor 100% boundary case | EN constant HIGH, no PWM edges, OUT1=3.6V OUT2=1.4V | **2** | BENCH-PWM |
| LED brightness at 50% PCA duty | emu8051 → adapter → board: **0.07248**, analytic: **0.07246** (0.03%) | **2** — found the adapter time-zero bug (all edges at t=0). Self-consistency could not have found it. | BENCH-PWM: milliamp measurement on LED |
| PCA PWM rate | Both: **7.2K edges/sec** (SYSclk/12/256 = 3600 Hz) | **2** — same arithmetic, separate routes | BENCH-PWM |
| Relay coil decode (active-low) | P2.0=0.6V → energised=true. No spurious interrupt enable. | **2** | A relay clicking on silicon |
| Button contact closure | Open: 5.0V / readPin=1. Pressed: 0.0V / readPin=0. INT0 NOT enabled. | **2** | BENCH-ADC (the same flash tests GPIO read) |
| ADC register sequence | ADC_CONTR=0xE0 (powered), P1ASF=0x02, P1M1 high-Z. START→FLAG→clear. | **2 register sequence only** — analog path open | **BENCH-ADC**: the analog path is the first bench question |
| UART TX bit period | ucsim: **86.8 µs** at 115200 baud, 3.1 µs polling residual | **3** — ucsim only; emu8051 has no bit timing at all (UART-ENTRY-POINTS.md §9), so no second implementation exists to disagree — by construction, not neglect. | **BENCH-UART**: a real UART answering HELLO |
| Idle-timeout resync | Firmware recovers from torn frame (SOF+garbage) after 10ms gap. Reply: **13 bytes** (SOF 0x7E, CMD 0x81, LEN 9). All 4 pre-registered predictions confirmed, none adjusted. Positive control: valid HELLO produced a reply on the same channel, so "no reply to garbage" is an observation, not an absence. | **3** — single emulator with bit timing (86.8 µs/byte); emu8051 cannot exercise this path (instant bytes — by construction, not neglect). Strong for a category 3 row: the predictions were pre-registered and the absence claim carries a positive control. Four bugs found on the path — piped stdio SIGTERM, `-e run` bypasses inject, inject during init, and the false-positive assertions below. | **BENCH-UART** |
| Cube refresh rate | **124.1 Hz** against 124 predicted. Invisible to the eye. | **2** — prediction still open until silicon | **BENCH-CUBE**: visible flicker = model wrong |
| Cube polarity | All four codebases assume active-HIGH from same vendor tables | **2** — shared source, cannot catch a shared misreading | **BENCH-CUBE**: photograph of lit LED at (FE,01) |
| 347-image corpus sweep | 0 genuine disagreements between emu8051 and ucsim. Re-run after the CLR/SETB fix: **2 images changed category** (strict 131→132, prefix 110→109, interleave 20→19, timing-count 33→34) — reclassification of boundary cases, not regression. "Unchanged" was claimed and then retracted (`0d022b7` → `b9dcae8`). Per-image identification not completed (12T timeout). | **1 (lineage)** — different upstream projects, but both modified this campaign. Independent lineage is not independent interpretation. | Silicon running one of the 347 images |
| 70 ngspice golden circuits | Stated tolerances per test file | **1** — ngspice is an independent reference solver | — |
| RC charge/discharge vs analytic | Within **5%** of V(t)=VCC*(1-e^(-t/RC)) | **2** — both are our own code | Oscilloscope on a real RC circuit |
| 555 astable period | **214 ms** vs 207.9 ms analytic (3%) | **2** | Oscilloscope on a real 555 circuit |
| NeoPixel WS2812B timing | ucsim: T0H=**362 ns** (250–550✓), T1H=**814 ns** (650–950✓), T0L=**814 ns** (700–1000✓), T1L=**452 ns** (300–600✓). All four windows pass. 72 bits (9 bytes), inter-byte gap 3074 ns, send 103.7 µs, pin LOW after → latch met. | **3** — measured from ucsim only. **This row was stale:** it said the two emulators disagree on CLR/SETB/CPL, with emu8051 at 3 MC. That defect was fixed in emu8051 `6cb9bc7` (confirmed correct at 1 MC by ucsim `8350048`), so the disagreement no longer exists and the reason for not cross-checking has gone with it. | **Re-measure on the fixed emu8051** — that is now available and would move this row to category 1 without touching hardware. Then a real WS2812B strip showing the correct colour. |
| I2C SCL timing (100 kHz) | ucsim measured at P2.2: 1T **t_HIGH 5.61 µs** (≥4.0 required, margin +1.61), **t_LOW 7.26 µs** (≥4.7, margin +2.56). 12T: 15.19 / 36.89 µs. Requirement cited to NXP UM10204 table 10. The previous loop count gave **t_HIGH 3.25 µs on 1T — 0.75 µs under spec** (see defects). Prediction before the re-measure was ~6.5 µs; actual 5.61, a 14% miss whose residual is ~0.89 µs of fixed per-call overhead. | **3** — ucsim only | **A logic analyser on real SCL.** Also worth having: the same measurement under a second model, which does not exist for I2C today. |
| SDCC native vs WASM byte-identity | Native SDCC 4.5.0 and the WASM four-stage pipeline emit **byte-identical 172-byte firmware**, same origin, **no injected records** (`emu8051-stc` spec-update 009). The comparison gate was itself verified: a deliberately corrupted image produces exit 1. | **2** — same source (SDCC 4.5.0) compiled two ways, so this checks the build and the pipeline, not the compiler's correctness. Not category 1: the cross-check is SDCC against itself. | Nothing here reaches silicon. It would rise if a *different* toolchain produced the same image — which no second 8051 compiler in this project does. |
| AVR blink under avr8js | Compiled by the hosted avr-gcc endpoint, executed through bw-board's adapter: LED brightness **0.5882**, derived beforehand from VCC 5 V, R 220 Ω, LED Vf 2 V / Rd 10 Ω, pin Rth 25 Ω → I = 11.76 mA. Exact match. An earlier "close to 0.68" was wrong — it omitted the LED's dynamic resistance. | **3** — avr8js is a third implementation with no cross-check in this project, and **no AVR silicon has run anything**. | An ammeter on a real Nano, or a second AVR simulator |

## Defects found and fixed on the path

These are as valuable as the measurements — each produced a plausible wrong
answer that would have shipped without the end-to-end check.

| What was wrong | How it presented | What found it |
|----------------|-----------------|---------------|
| `IE = 0x00` — PCA ISR never fires | Servo driver compiles, flashes, does nothing | bw-board e2e test (`74671d5`) |
| `IE.6` is ELVD, not EC — setting it enables Low Voltage Detection, not PCA | Bogus `EC=1` in emitted C, agreed on by two agents | SDCC's own `stc12.h` header (category 1 — independent source) |
| PCA interrupt enable is ECCF in CCAPMn, not a bit in IE | Both emulators checked IE instead of CCAPM | Contract correction (`02dd84e`), not a measurement |
| EA after CR — init race, first CCF0 lost | ISR declared but never entered, relay/servo silently dead | bw-board analysis of the match-at-zero race |
| `advanceTo` never called `_updateDevices` | Relay with 5ms delay never energised without pin activity | bw-cfront relay example assertion |
| Adapter push-mode: `advanceTo` not called before `setPin` | All 214 PWM edges at time zero, brightness = 0 | Cross-model brightness check (self-consistency could not find it) |
| L293D pinout: right-side pins 9-16 scrambled in sidecar | Part art teaches wrong wiring | bw-parts checking against datasheet |
| MCU sidecar: generic 8051 pins (PSEN/ALE/EA instead of P4.4/P4.5/P4.6) | Terminal cross-check passed by excluding MCU | bw-parts checking against PINOUT.md |
| `§4.6` citation fabricated — per-pin currents are in §4.1 | Four bilingual docs cited a section that says something else | Reading the actual datasheet PDF |
| `aggregateCurrent()` defined inside its own test, nowhere in src | DRC warning existed only as a test, not as real code | Grepping for the function name |
| Current rating schema change (`{chip_mA, supply_mA}`) — old vendored copy | `0 + 'circuit'` = `'0circuit'`, comparisons silently false | Coordinator grep within an hour of the change |
| ucsim `CL`-wrap bug in 16-bit compare | PCA compare/match fired at wrong count | ucsim's own measurement against expected period |
| Flag gate fetched 4.7 MiB unconditionally | `import()` fired regardless of flag; flag check was inside the imported module | Playwright request interception (measuring the untriggered case) |
| NeoPixel driver sent all zero bits regardless of colour | `bw_neo_byte` rotated `A` while byte sat in `DPL` — all bytes identical, so the byte-count bug below was invisible | ucsim-stc measurement of the bit stream |
| NeoPixel driver sent only 1 of 9 bytes | Inline `djnz r7` clobbered the R7 SDCC's outer loop used as its byte counter — invisible while all bytes were zeros | ucsim-stc re-measurement after the all-zeros fix |
| NeoPixel R6 assumed free by first fix | Register swap relied on a claim about SDCC's allocator, not a check of the listing. `push ar7`/`pop ar7` verified against generated assembly is what held. | Checking the `.asm` output rather than trusting the assumption |
| ROADMAP claimed "ADC proven on real hardware" | No bench session has happened; the claim was introduced in a doc-levelling pass | Coordinator reading the document (`4eaa84f`) |
| emu8051 CLR/SETB/CPL bit = 3 MC (should be 1 MC per MCS-51 spec) | Opcodes returned 2 (tickDelay convention: `(return+1)*scale-1` = 3 MC at 1T). Bit-banged timing 3× too slow. Servo 0.4 µs spread is 90% this defect. Commit `6cb9bc7` title says "2 cycles" — **title is wrong**, should say "3 cycles" (return 2 → 3 MC total). Ledger row is correct at 3 MC. Fixed to return 0 (= 1 MC). | steveschnepp/emu8051 sweep; ucsim-stc `8350048` confirmed 1 MC correct. |
| Resync test: piped stdio filled 200KB trace buffer → SIGTERM | stc12_trace killed before TX file written, test read 0 bytes | Switching from execFileSync(pipe) to spawnSync(ignore) |
| Resync test: `-e run` bypasses `-inject` dispatch | Inject fires in stc12_trace's own `-until-ns` loop, not in `-e`'s interactive interpreter. Two mutually exclusive execution loops. | ucsim-stc `477d5d2` identified the dispatch path |
| Resync test: inject fired during init, before UART configured | SCON/BRT not yet written when byte arrived. RI set but firmware not listening. | Derived inject time from firmware listing; 29ms margin past SCON write |
| Resync test: false positive — assertions matched PC hex digits | `result.includes('7e')` matched PC address 17E4, not UART TX byte 0x7E | Positive control (prediction 4) would have caught it; switching to TX file capture |
| I2C `i2c_delay` ran the bus out of spec on 1T | `for(i<2)` gave **t_HIGH 0.72 µs against a 4.7 µs minimum** — six times too fast on the STC12, in the driver behind the I2C LCD. The first fix (loop 13) was still 0.75 µs under, because `t_LOW ≥ 4.7 µs` and `t_HIGH ≥ 4.0 µs` are **two different minimums** and one number was used for both. | Measurement, not arithmetic — ucsim measured SCL at the pin twice. The derivation had been checked and believed. |
| ADC settle loop counted instructions, not time | Same class as the 4.7 µs ultrasonic trigger: a loop whose duration differs between 1T and 12T for identical code. Replaced with 8 NOPs (datasheet §10.5, ~8 oscillator clocks). | A sweep for delay loops that are neither Timer 0 nor PCA — found by searching for the pattern rather than noticing it |
| `servo` and `motor` were silent no-ops on STC89 | STC89 has no PCA, so the driver emitted no edges at all: no error, no warning, nothing in the generated C. Refused at compile time now, as WS2812-on-12T already was. | Reading the 12T line of a servo measurement that was otherwise clean ("0 edges — expected") |
| `ultrasonic` was a silent no-op on STC15W408AS | That part has no Timer 1. Found by the availability sweep that the servo case prompted, not by a test. | Sweeping every driver against part capability instead of fixing one case |
| SDCC WASM: `thisProgram` unset under Emscripten MODULARIZE | `sdas_init()` checks `argv[0]` starts with `sdas`. It did not, so **all** sdas-specific behaviour was off at once — no `addr` field on A records, `.optsdcc` ignored, no `O` record. Presented as a 1-byte code-origin shift that survived three injections and four wrong root causes. | Elimination: five candidates excluded by experiment, then the surviving one demonstrated directly (same `.rel`, two linkers, two placements) |
| `sdld` `newarea()` trusts `eval()` on a field that may be absent | Reads past the end of an A record with no `addr` and uses whatever it returns. Undefined behaviour that resolves to 0 natively and 1 under wasm — which is why two builds of identical source disagreed. **Upstream, latent, not fixed** — our fix makes it unreachable, not correct. | The same elimination; worth reporting upstream with the reproduction |
| Schematic projection emitted zero wire nets | A 20-symbol circuit rendered every part and no connections: wire endpoints used friendly names (`data`/`clock`), sidecar terminals used DIP names (`ser`/`srclk`), and `_syncNetlist` silently matched nothing. All symbols present, correct font, no overlaps — and zero information. | A browser check that had been left open as "unverified" rather than closed on the model being correct |
| Vendored sidecars drifted 115 vs 123, with 4 renames | The missing 8 are visible; the 4 renames are not — a copy-only sync leaves both names present, so one part exists twice with divergent data. | bw-parts counting its own catalogue against what the consumer had vendored |

## Bench questions and the rows they settle

| Bench ID | Rows it settles | What to measure |
|----------|----------------|-----------------|
| **BENCH-ADC** | ADC register sequence (analog half), button GPIO read | Blink period at pot extremes, smooth tracking |
| **BENCH-CUBE** | Cube polarity, cube refresh rate | Which LED lights at (FE,01), visible flicker |
| **BENCH-UART** | UART TX bit period, **idle-timeout resync** | Whether HELLO answers, halt skew in ms, recovery after a torn frame |
| **BENCH-PWM** | Servo pulse (0/90/180 and frame), motor duty (register and pin), motor 100% boundary, LED brightness, PCA rate | Milliamps through LED, frequency at CEX0 |

Four rows name no bench ID and have no instrument assigned: motor H-bridge
direction, relay coil decode, the RC and 555 circuits, and I2C SCL timing.
Three of those name an instrument in prose (a motor spinning, a relay clicking,
an oscilloscope); **I2C SCL needs a logic analyser and no bench question covers
it.** The 347-image sweep and the NeoPixel row are likewise unassigned. That is
not an oversight to hide — it is the list of claims the planned bench session
would leave exactly where they are.

## The principle

A ledger of successes is an advertisement. A ledger that includes what nearly
shipped is a record. Every row in the defects table produced a plausible wrong
answer that looked correct in the source.
