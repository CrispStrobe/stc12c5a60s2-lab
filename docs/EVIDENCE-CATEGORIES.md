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
- *(LED brightness was listed here but is **2b**: both sides were modified this
  campaign. It found a real bug — the adapter time-zero defect — which makes
  it valuable despite the lower category. See the ledger for the correct
  classification.)*

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

## Nobody re-checks the auditor

Two agents arrived at this independently on 2026-08-10, in the same hour:

> "the pass that audits claims is itself where claims get made, and its output reads as
> adjudicated. Nobody re-checks the auditor." — bw-board

> "The audit pass is where overclaims are most likely to land, because it reads as
> adjudicated. My spec-update 008 originally said 'IE.6 is shared between LVD and PCA'
> and that survived my own review because it sat beside correct vector addresses.
> Correct context laundering a wrong claim." — emu8051-stc

The coordinator is the worst case of this, and produced the worked example the same day.

**Four false findings on one question.** Asked whether the `stc12` extension reached the
deployed bundle, the answer was reported as "absent" four times. It was present throughout.

| attempt | what it did | why it looked right |
|---|---|---|
| 1 | grepped `overlay/` only | 16 hits, 6 of them false — overlay files patching upstream paths are imported by code the overlay cannot see |
| 2 | enumerated chunk URLs with a bad pattern | fetched **zero** chunks and reported "found in no chunk" |
| 3 | grepped `packages/scratch-gui/build/` | that directory did not exist; every "0 files" was vacuous |
| 4 | fetched 29/29 chunks from the webpack manifest, with a positive control | the manifest excludes chunks the document loads via `<script>` — it was 29 of 30, and the missing one held the extension |

Attempt 4 is the instructive one. It had a denominator, it had a positive control, and it was
still wrong — because both sat on top of an enumeration that silently excluded one chunk, and
the control lived in the chunks that *were* fetched. **A control only proves the method works
on the population you searched.**

Two further inversions followed from the same investigation: `require('scratch-vm')` resolves
the package `main` to `dist/`, which contains none of this project's built-in extensions, so a
node harness testing "does the VM load our blocks" measured the wrong VM and threw
`Worker is not defined` — twice, hours apart.

**What actually caught each one:** asking what the tool was looking at, rather than what it
returned. Not "is the string absent" but "did I read the file that would contain it".

### The countermeasures, in the order they earn their keep

1. **Print the denominator.** `fetched 29/29` is worth more than the result.
2. **Run a positive control** — something that must be found. If it is not, the method is broken.
3. **Choose the control from the same population**, as close to the subject as possible. A
   sibling in the same directory beats a distant relative; an upstream extension could not
   distinguish a `dist` build from a `src` build, which is exactly the distinction that mattered.
4. **Verify the population itself.** Does the directory exist? Does the enumeration cover every
   member, including the ones loaded by a different mechanism?
5. **When a finding survives all four and still contradicts someone else's measurement, suspect
   the measurement you control.** The other agent had the browser; I had greps.

None of this is exotic. All four failures were ordinary, and each produced an output that
looked exactly like evidence.

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
  "Assert presence, not position." (`1581233`)

**Corollary:** a check that has never failed has not been shown to work.
The flag gate that fetched 4.7 MiB regardless of the flag, and the
chip-budget warning that stayed green while the function was defined only
inside its own test, were both correct in the source.

## If it would pass with the thing absent, it needs a positive control

bw-board's rule, arrived at on 2026-08-10 after its own test lied to it twice
in one day. It is the sharpest form of the corollary above, and it is
mechanical enough to apply without judgement:

> **If a check would still pass with the measured thing absent, it needs a
> positive control.**

The test that produced the rule is worth recording in full, because it passed
convincingly. `serial-debug-e2e.test.js` asserted that the firmware answers a
torn frame after an idle timeout, and reported PASS (`a551258`). It was a
false positive: the assertions matched **hex digits inside PC trace
addresses**, not UART TX data. Zero TX bytes were produced at any run length.
The test would have passed with the UART entirely disconnected — which,
functionally, it was. Corrected in `9f69af9`, and the finding restated as
INCONCLUSIVE in `be62e14` rather than downgraded quietly or left green.

The same shape had already caught the byte-identity gate, where the metric was
saturated by construction: two 172-byte images offset by one byte differ in
~169 positions no matter what else is fixed, so "169 differences" could not
move and two successive real fixes looked identical to no fix at all.

Both were correct in the source. Both were read by people who knew what they
were looking for. What distinguishes them from an honest pass is not care but
a control: something that must be found for the method to be working, checked
alongside the thing being measured. Without it, a green result and a broken
harness are indistinguishable — and the harness is the more likely of the two,
because nothing tests it.
