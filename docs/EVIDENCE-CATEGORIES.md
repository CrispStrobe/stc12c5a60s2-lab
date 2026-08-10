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
