# Verification ledger — what this campaign proved, and what it did not

One row per claim. Four columns: what is claimed, the number, the evidence
category (per `EVIDENCE-CATEGORIES.md`), and what would raise it.

Nothing here has run on real silicon. The bench questions (by ID) name which
rows they would settle.

## Cross-model measurements

| Claim | Number | Category | What would raise it |
|-------|--------|----------|---------------------|
| Servo pulse width at 90° | emu8051: **1500.0 µs**, ucsim: **1499.6 µs** (0.4 µs spread) | **2b** — both PCA dispatches fixed after exchanging findings. Independent anchor: 1500 µs from FOSC/12 arithmetic. | **BENCH-PWM**: frequency counter on real CEX0 pin |
| Servo pulse at 0° / 180° | emu8051: **499.2 / 2500.6 µs** | **2b** | BENCH-PWM |
| Servo frame period | emu8051: **20003.5 µs** = 50.0 Hz, ucsim: **20000.0 µs** | **2b** | BENCH-PWM |
| Motor PWM duty (8-bit) | ucsim: 84/128/192 of 256 counts. Period **277561 ns** unchanged. | **2b** — independent anchor: counts are what the driver arithmetic fixed. | BENCH-PWM |
| Motor H-bridge direction decode | Board: IN1=5V/IN2=0V → FORWARD, IN1=0V/IN2=5V → REVERSE | **2b** | A motor visibly spinning on silicon |
| Motor 100% boundary case | EN constant HIGH, no PWM edges, OUT1=3.6V OUT2=1.4V | **2b** | BENCH-PWM |
| LED brightness at 50% PCA duty | emu8051 → adapter → board: **0.07248**, analytic: **0.07246** (0.03%) | **2b** — found the adapter time-zero bug (all edges at t=0). Self-consistency could not have found it. | BENCH-PWM: milliamp measurement on LED |
| PCA PWM rate | Both: **7.2K edges/sec** (SYSclk/12/256 = 3600 Hz) | **2b** — same arithmetic, separate routes | BENCH-PWM |
| Relay coil decode (active-low) | P2.0=0.6V → energised=true. No spurious interrupt enable. | **2b** | A relay clicking on silicon |
| Button contact closure | Open: 5.0V / readPin=1. Pressed: 0.0V / readPin=0. INT0 NOT enabled. | **2b** | BENCH-ADC (the same flash tests GPIO read) |
| ADC register sequence | ADC_CONTR=0xE0 (powered), P1ASF=0x02, P1M1 high-Z. START→FLAG→clear. | **2b register sequence only** — analog path open | **BENCH-ADC**: the analog path is the first bench question |
| UART TX bit period | ucsim: **86.8 µs** at 115200 baud, 3.1 µs polling residual | **2b** — baud not modelled by emu8051 | **BENCH-UART**: a real UART answering HELLO |
| Cube refresh rate | **124.1 Hz** against 124 predicted. Invisible to the eye. | **2b** — prediction still open until silicon | **BENCH-CUBE**: visible flicker = model wrong |
| Cube polarity | All four codebases assume active-HIGH from same vendor tables | **2b** — shared source, cannot catch a shared misreading | **BENCH-CUBE**: photograph of lit LED at (FE,01) |
| 347-image corpus sweep | 0 genuine disagreements between emu8051 and ucsim | **Cat 1 (lineage)** — different upstream projects, but both modified this campaign. Independent lineage is not independent interpretation. | Silicon running one of the 347 images |
| 70 ngspice golden circuits | Stated tolerances per test file | **Cat 1** — ngspice is an independent reference solver | — |
| RC charge/discharge vs analytic | Within **5%** of V(t)=VCC*(1-e^(-t/RC)) | **2b** — both are our own code | Oscilloscope on a real RC circuit |
| 555 astable period | **214 ms** vs 207.9 ms analytic (3%) | **2b** | Oscilloscope on a real 555 circuit |

## Defects found and fixed on the path

These are as valuable as the measurements — each produced a plausible wrong
answer that would have shipped without the end-to-end check.

| What was wrong | How it presented | What found it |
|----------------|-----------------|---------------|
| `IE = 0x00` — PCA ISR never fires | Servo driver compiles, flashes, does nothing | bw-board e2e test (`4f14c35`) |
| `IE.6` is ELVD, not EC — setting it enables Low Voltage Detection, not PCA | Bogus `EC=1` in emitted C, agreed on by two agents | SDCC's own `stc12.h` header (category 1 — independent source) |
| PCA interrupt enable is ECCF in CCAPMn, not a bit in IE | Both emulators checked IE instead of CCAPM | Contract correction (`e9a3f02`), not a measurement |
| EA after CR — init race, first CCF0 lost | ISR declared but never entered, relay/servo silently dead | bw-board analysis of the match-at-zero race |
| `advanceTo` never called `_updateDevices` | Relay with 5ms delay never energised without pin activity | bw-cfront relay example assertion |
| Adapter push-mode: `advanceTo` not called before `setPin` | All 214 PWM edges at time zero, brightness = 0 | Cross-model brightness check (self-consistency could not find it) |
| L293D pinout: right-side pins 9-16 scrambled in sidecar | Part art teaches wrong wiring | bw-parts checking against datasheet |
| MCU sidecar: generic 8051 pins (PSEN/ALE/EA instead of P4.4/P4.5/P4.6) | Terminal cross-check passed by excluding MCU | bw-parts checking against PINOUT.md |
| `§4.6` citation fabricated — per-pin currents are in §4.1 | Four bilingual docs cited a section that says something else | Reading the actual datasheet PDF |
| `aggregateCurrent()` defined inside its own test, nowhere in src | DRC warning existed only as a test, not as real code | Grepping for the function name |
| Current rating schema change (`{chip_mA, supply_mA}`) — old vendored copy | `0 + 'circuit'` = `'0circuit'`, comparisons silently false | Coordinator grep within an hour of the change |

## Bench questions and the rows they settle

| Bench ID | Rows it settles | What to measure |
|----------|----------------|-----------------|
| **BENCH-ADC** | ADC register sequence (analog half), button GPIO read | Blink period at pot extremes, smooth tracking |
| **BENCH-CUBE** | Cube polarity, cube refresh rate | Which LED lights at (FE,01), visible flicker |
| **BENCH-UART** | UART TX bit period | Whether HELLO answers, halt skew in ms |
| **BENCH-PWM** | Servo pulse, motor duty, LED brightness, PCA rate | Milliamps through LED, frequency at CEX0 |

## The principle

A ledger of successes is an advertisement. A ledger that includes what nearly
shipped is a record. Every row in the defects table produced a plausible wrong
answer that looked correct in the source.
