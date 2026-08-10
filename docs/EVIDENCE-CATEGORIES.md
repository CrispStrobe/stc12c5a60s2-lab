# Evidence categories for cross-model claims

Canonical definition. All repos reference this file rather than restating it.
Three categories, one numbering, one sentence underneath all of it:

> **Silicon remains the only source independent of every document.**

## Category 1: Independent-source agreement

Two implementations whose information came from **different places** — a
different upstream codebase written by different people, an independent
reference solver with its own validation history, or a physical measurement.

This is the strongest cross-check short of hardware.

**Examples in this project:**
- 347-image corpus sweep: emu8051 (fork of jcmvbkbc/emu8051) vs ucsim
  (fork of sdcc.sourceforge.net/ucsim) — different upstream projects,
  different authors, different architectures.
- 70 ngspice golden circuits: bw-board's MNA solver vs ngspice, a mature
  open-source SPICE implementation with decades of independent validation.
- LED brightness cross-check: emu8051's PCA model (C) → adapter → bw-board's
  brightness integrator (JS). Found the adapter time-zero bug that
  self-consistency could not have detected.

**Limit of this category:** emu8051 and ucsim share no upstream code, but both
were read and modified by agents in this campaign, and both were checked
against the same STC datasheet. Independent upstream lineage is not the same
as independent interpretation. The 347-image sweep is the strongest claim in
the project; it should carry this caveat rather than have one discovered later.

**What is stronger:** a physical measurement on real silicon. That is the bench
session, and it is the only thing above category 1.

## Category 2: Same-source agreement

Two implementations both derived from **the same document** or from each other.
This catches arithmetic errors, transcription slips, and drift — genuinely
useful, and it is most of what we have.

**It cannot catch a misreading of the source.** Four codebases agreeing on
active-high polarity, all reading the same vendor animation tables, is the
clearest example. A shared misreading produces identical agreement.

Each category 2 claim should name **what would move it to category 1** — a
specific measurement, instrument, or independent source. Without that column
the ledger is decorative.

**Examples:**
- Cube polarity: four codebases assume active-HIGH from the same tables.
  → Moves to cat. 1 with: a photograph of a lit LED at `(FE, 01)`.
- PCA PWM rate (7.2K edges/sec): both sides derived from SYSclk/12/256.
  → Moves to cat. 1 with: a frequency counter on the real CEX0 pin.
- Serial codec: five implementations agree on the wire format.
  → Moves to cat. 1 with: a logic analyser trace on a real UART.
- Cube brightness: bw-board and bw-circuit-ui use the same 12.5% duty model.
  → Moves to cat. 1 with: a current measurement during a known scan pattern.

## Category 3: Single-implementation assertion

One model, no cross-check. Honest and weakest. Most device model parameters
(relay coil resistance, motor constants, servo slew rate) fall here — they
are plausible engineering estimates with no second source.

**Examples:**
- Boundary A conformance suite: tests that the adapter satisfies the contract,
  not that the contract matches hardware.
- Device model defaults (relay 200Ω coil, motor kV=0.01, etc.): order-of-
  magnitude fits from typical datasheets, not verified against specific parts.

## The case that made this concrete

Everything above was reasoning until 2026-08-10, when the failure it predicts
actually happened. It is worth recording precisely, because a rule people
accept and a rule people apply are different things.

**The check:** bw-circuit-ui cross-checked its terminal definitions against
bw-parts' 115 sidecars — two agents, two repos, one comparison. It passed.

**The error it did not find:** bw-parts' MCU sidecar carried the generic 8051
trap. Pin 10 was labelled `rxd` rather than `P3.0`, and pins 29–31 were
`psen`/`ale`/`ea` — signals this part does not have, because STC removed
external memory addressing. Those pins are `P4.4`/`P4.5`/`P4.6` GPIOs. Part
art mislabelled that way teaches the wrong pinout to everyone who looks at it.

**Why the cross-check stayed green:** it skipped MCU, breadboard and meter as
"deliberately different". The exclusion was defensible in itself. It also meant
the one part carrying a real error was the one part not compared.

**What found it:** bw-parts checking its own sidecar against `docs/PINOUT.md`
and the datasheet — a source, not another agent.

Three things follow, and they are the argument for this whole file:

1. **Category 2 agreement can be perfect while one side is wrong.** Here it was
   worse than perfect: the check could not have disagreed, because the
   disagreeing part was outside it.
2. **Every exclusion in a cross-check is an unchecked claim.** "Deliberately
   different" is a reason to compare differently, not a reason to skip. When
   you exclude something, write down what now goes unverified.
3. **Going to the source is not ceremony.** It was the only thing that worked,
   and it took one agent reading a pinout table.

Cost had it survived: the drawings are what a beginner trusts most, because
they look like the chip in front of them.

## Using this classification

When writing "X and Y agree", add where X and Y got their information. One
clause. If the answer is "the same paragraph of the same PDF", the reader
needs to know — that is category 2, not category 1.

When filing a new cross-model result, state:
1. What the two implementations are
2. Where each got its information (same source? different source?)
3. What would move the claim up one category

A prediction written before a measurement is the only instrument that detects
a shared misreading. The bench predictions in `BENCH-SESSION.md` exist for
exactly this reason.

## Assert the property, not the symptom

Four repos arrived at this independently on 2026-08-10. It belongs beside
the taxonomy because the two are the same idea at different scopes: the
taxonomy is about what evidence is worth; this is about what a test actually
establishes.

**The rule:** testing for the absence of the specific wrong thing catches
only the wrong thing you already thought of. Asserting what something IS
catches the whole class, including the error nobody imagined.

**Real cases from this campaign:**

- **Absence tests pass by omission.** A terminal cross-check excluded the
  MCU as "deliberately different" and passed — while the MCU sidecar
  carried wrong pin names. Asserting "PSEN is absent" would also have
  passed a sidecar with every pin shifted by one, or P0 ascending, or
  `rxd` where `P3.0` belongs. (bw-parts `a78ff11`)
- **Presence tests cannot.** "Pin 32 IS P0.7, P0 runs descending, RST/GND/VCC
  are at their correct positions" fails loudly on any wrong map, including
  wrong maps nobody has thought of. (bw-bundle pin-map test)
- **Set equality beats spot checks.** bw-blocks asserts the emitter and
  decompiler function sets are equal — so a `createBlock` call with no
  corresponding `case` label fails the census, rather than passing because
  nobody remembered to add it to a hand-kept list.
- **Position assumptions break on real parts.** bw-circuit-ui found that
  not all DIP-14 chips have VCC on pin 14 — `74hc73` puts VCC on pin 4.
  "Assert presence, not position." (`568b5dc`)

**Corollary:** a check that has never failed has not been shown to work.
The flag gate that fetched 4.7 MiB regardless of the flag, and the
chip-budget warning that stayed green while the function was defined only
inside its own test, were both correct in the source.
