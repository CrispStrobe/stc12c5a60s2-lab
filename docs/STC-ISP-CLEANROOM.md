# STC ISP in Rust — the clean-room contract

Goal: a Rust implementation of the STC serial bootloader protocol
(an stcgal-equivalent for the parts this lab owns), licensed by us.
Working name: **`stcbsl`**, living at `tools/stcbsl/` in this repo,
**MIT** (uniform with the rest of this repo; explicitly NOT a port).

## Why a clean room

stcgal is GPL-3.0. A port, translation, or "rewrite with the Python
open in the next window" is a derivative work and stays GPL-3.0 —
it can never become MIT/MPL. What is NOT copyrightable is the
protocol itself: byte layouts, handshakes, checksums, timings are
facts. The ledcube example (`src/20-ledcube/`, spec
`../ucsim-stc/spec-updates/008-ledcube-hardware-spec.md` §7) is this
repo's precedent: one agent measures behavior and writes a spec of
facts, a different agent who never saw the original writes the code,
and the provenance section records what each side was permitted to
read.

## Roles and what each may read

**Bench (capture) role** — records byte-exact transcripts of real
stcgal sessions against our own silicon (STC89C52RC first — it is on
the bench, BSL 6.6C, CH340). Transcripts are facts about what bytes
crossed our wire to our chip. MAY run stcgal (running GPL software
produces no derivative). MUST NOT commit any stcgal source or text
into this repo — logs and bench notes only. Output:
`docs/isp-captures/stc89c52rc/` (raw logs verbatim + NOTES.md with
command lines, stcgal version, chip identity, adapter, OS).

**Spec role** — writes `docs/STC-ISP-PROTOCOL.md` (English-only
internal doc, like the peripheral models) from the captures plus
public fact sources: STC's own datasheets and app notes, and
non-GPL protocol write-ups read for FACTS with nothing copied.
MUST NOT open stcgal's source or its repository documentation, nor
any other GPL ISP tool's source (stcflash etc.). The spec carries a
provenance section listing exactly what was consulted.

**Implementation role** — writes the Rust crate from the SPEC and
the CAPTURES ONLY (captures double as replay-test fixtures). MUST
NOT open stcgal or any GPL ISP tool, in any language, at any point —
including their documentation. Designs its own CLI surface (no
flag-for-flag clone). If the spec is ambiguous, the fix is a
question back to the spec role or a new capture — never a peek.

**Coordinator** (this file's author) sequences the three and keeps
the roles from bleeding.

## Risk notes

- `stc8prog` (C) advertises MIT, but we have not audited its
  ancestry; the rgm3/ledcube444 lesson applies (an upstream with no
  licence to give cannot be laundered by a later MIT label). It is
  NOT an approved source for any role.
- stcgal's `-D`/debug output, when the bench role captures it, is a
  byte dump of a wire session — facts, admissible. stcgal's prose
  and code are not.
- Scope v1: the stc89 protocol (12T classic, on the bench today).
  The spec is structured so stc12/stc15 chapters can be added when
  those boards return to the bench; the crate's protocol layer is a
  trait for the same reason.

## Definition of done

1. Spec covers the full stc89 session: handshake/pulse detect, info
   frame (chip ID, BSL version), baud negotiation, erase, program
   (block size, checksums, acks), option bytes, reset-to-app.
2. Crate `tools/stcbsl` passes replay tests against every capture,
   headless.
3. The real bench run: `stcbsl` flashes `01-blink` onto the actual
   STC89C52RC and the owner sees it blink — the same bar every
   silicon claim in this repo meets.
4. Provenance sections complete in both spec and crate README.

## Coordinator rulings (append-only)

- **2026-08-17 — `../stc-compiler`'s STC12 flasher is OFF LIMITS to the
  spec and implementation roles.** Its own `BENCH-FLASHING.md` describes
  the flasher as "byte-identical to what stcgal itself emits," which
  makes it stcgal-derived in substance regardless of any local licence
  label — the rgm3/ledcube444 lesson (an upstream with no licence to
  give cannot be laundered downstream). `stc12-session.json` from the
  same source is excluded for the same reason. When stc12 support is
  specced later, its facts come from fresh bench captures, never that
  flasher.
- **2026-08-17 — the CSDN `james026` write-up is refused.** The spec
  role opened it in phase 1, found it declares GPL v3 and embeds tool
  source, and excluded it; the open items it would have answered were
  re-derived from our own captured bytes instead. Not an admissible
  source for any role.
