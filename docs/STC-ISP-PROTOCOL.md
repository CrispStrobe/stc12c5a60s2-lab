# The STC serial ISP protocol — specification

Status: **phase 2 — the STC89 chapter is implementation-grade.** Every
byte-level statement below about the STC89 is derived arithmetically from
the nine bench captures in `docs/isp-captures/stc89c52rc/`, taken against
this lab's own STC89C52RC on 2026-08-17, and is reproducible from them.
Where the captures are silent the item is marked **`[NEEDS-BENCH]`** and
carries a one-line experiment that would settle it. Nothing here is a
guess dressed up as a fact.

This is the document the Rust implementation (`tools/stcbsl`) is written
from. Its governing contract is [`STC-ISP-CLEANROOM.md`](STC-ISP-CLEANROOM.md):
the implementation role reads **this file and the captures, nothing
else**. A wrong sentence here becomes a wrong line of Rust with no second
opinion available.

English-only, like the other internal technical documents in this repo.

**Reading order for an implementer:** §3 (physical layer) → §4 (frame
grammar) → §5 (session) → §6 (state machine) → §12 (what is still open).
§7 is the arithmetic behind the frequency handling and is worth reading
before writing the baud code.

---

## 1. Scope

- **v1 target: the STC89 protocol** — an **STC89C52RC**, BSL **6.6C**,
  8 KB code flash, 6 KB EEPROM flash, on a Prechin 51-单核-A2 board with
  an on-board CH340C, driven from macOS. §7–§9 are the STC89 chapters.
- STC12 (§10) and STC15 (§11) are stubs. They are **different protocol
  generations, not dialects**; nothing in §4–§9 may be assumed to carry
  over.
- Out of scope: USB programmers (U8W and friends), parallel programming,
  and IAP/EEPROM access *from the running application* (the `ISP_*` SFR
  block — a different mechanism that shares the name).

| Term | Meaning |
|---|---|
| **BSL** | the bootstrap loader resident in the chip's ISP boot region |
| **host** | the PC side (our future `stcbsl`) |
| **MCU** | the target chip running the BSL |
| **frame** | one delimited `46 B9 … 16` message |
| **session** | cold power-on through to the MCU running user code |
| **block** | 256 bytes — the erase unit (§5.5), *not* the write unit |

---

## 2. Provenance

The clean-room rule (`STC-ISP-CLEANROOM.md` §2): this document may be
written from vendor documentation, from independent non-GPL write-ups
read for facts, and from byte-exact measurements. It may **not** be
written from stcgal or any other GPL-licensed ISP tool's source or
documentation.

### 2.1 Primary source — our own captures

`docs/isp-captures/stc89c52rc/` — nine verbatim debug logs of real
sessions against our silicon, plus `NOTES.md` with command lines,
versions and chip identity. The capture role ran the stcgal *binary* and
recorded its wire dump; running a GPL program produces no derivative
work, and the bytes on our wire to our chip are facts. No stcgal source
or repository documentation was opened by the capture role or by this
one.

Normalized frame tables are committed beside them in `frames/*.jsonl`
(schema: `tools/isp-capture/README.md`). Citations below are of the form
`[02-erase-run1:38]` = that log, that line number; the `src` field of
every JSONL record carries the same coordinate.

**One limitation, stated up front:** these are *host-tool* logs, so they
show every byte the host received and every framed packet it sent, but
**not** the host's own pre-handshake pulse train. §5.1 is therefore the
one part of the session still resting on public sources.

### 2.2 Vendor documentation

| Tag | Source |
|---|---|
| `[DS89]` | *STC89C51RC/RD+ series datasheet*, STC MCU Limited, English, 2011-07-25, 271 pp. Owner-supplied copy; origin URL <https://www.mikrocontroller.net/attachment/449642/STC89C51RC-english.pdf> (mirror of www.STCMCU.com). Cited by section: §1.9 naming rules, §1.10 90C/HD versions, §2.2.5 warm/cold boot, §10.1 ISP principle, §10.2 ISP application circuit, §10.3 PC-side usage. The PDF is **not** committed to this repo. |
| `[STC-TOOL]` | *STC Tool Instruction Manual*, English, revision dated 2022-03-09, `stcmicro.com/pdf/stc-tool-en.pdf`, §2 "System Programmable (ISP) Process Description" |

### 2.3 Independent public write-ups (phase 1)

| Tag | Source | Used for |
|---|---|---|
| `[NCRMNT]` | "Reverse engineering STC ISP protocol", ncrmnt.org, 2011-10-14 — a sniffed transaction log from an STC 8051 of that era | Structural corroboration and the `0x7F` sync train, which our captures cannot show |
| `[EMBDEV]` | thread `embdev.net/topic/383764` | The falling-edge entry mechanism |
| `[GRAUONLINE]` | `grauonline.de/wordpress/?p=44` | Start-tool-then-power-on ordering |

**Agreements found (§2.6 lists them in detail):** `[NCRMNT]`'s log shows
the same `46 B9` preamble, the same `0x68`/`0x6A` direction bytes and the
same `0x16` terminator that our captures show, on different silicon 15
years earlier. Where its log and ours disagree — frame lengths, payload
values, checksums — ours wins: its published log is demonstrably lossy
(§2.5) and ours validates 149/149 frames against a single arithmetic
rule.

### 2.4 Sources deliberately NOT read

In a clean room the refusals matter as much as the citations.

- **stcgal** — GPL-3.0. Repository, forks, mirrors, readthedocs, and PyPI
  project page: all off-limits, all skipped unread when they surfaced in
  search results. No stcgal artefact of any kind was opened at any point
  in the writing of this document, in either phase.
- **stcflash** and any other GPL-licensed ISP tool source — same rule.
- **`stc8prog`** — not approved by the contract (MIT label, unaudited
  ancestry). Not opened.
- **`github.com/van9ogh/stc-isp`**, **`codeberg.org/azman/my1stcflash`** —
  third-party tools of unaudited ancestry. Not opened.
- **`../stc-compiler`'s STC12 web flasher and `scripts/fixtures/stc12-session.json`** —
  *our own sibling repo*, and therefore the most tempting of all.
  Deliberately not consulted: this repo's `docs/BENCH-FLASHING.md`
  describes that implementation as "byte-identical to what stcgal itself
  emits", which reads as stcgal-derived provenance. Laundering it through
  an internal repo would defeat the exercise. **Coordinator decision
  needed** before it is ever admitted; until then, tainted.
- **Scribd re-hosts of STC manuals** — vendor PDFs were reachable
  directly.

### 2.5 One source opened and then excluded

A Chinese write-up 《STC单片机的下载协议》 (CSDN, author `james026`, 2014;
a `51hei.com` page merely redirects to it) was opened in phase 1 while
searching for independent descriptions. Its second paragraph declares the
**article itself to be released under GPL v3**, and it embeds source code
of the author's own flashing tools.

It is therefore **excluded**. Nothing from it is asserted anywhere in
this document and no tag refers to it. The reader deserves to know that
it offers concrete answers to several items that are still marked
`[NEEDS-BENCH]` in §12; they are not reproduced, not paraphrased, and not
smuggled into the checklist as leading questions. Every fact in §4–§9 was
derived by arithmetic on our own captures, and each states the
computation so it can be re-run.

`ceptimus.co.uk`'s STC89C51/C52 programmer write-up was also opened and
also excluded, for a different reason: it contains no protocol-level
facts at all, and states that its implementation derives from a
third-party repository of unaudited provenance.

### 2.6 Cross-checks against the public record

| Claim | Public source | Our captures | Verdict |
|---|---|---|---|
| Frames open `46 B9` | `[NCRMNT]` | 149/149 frames | **agree** |
| `0x68` = MCU→host, `0x6A` = host→MCU | `[NCRMNT]` (27/27 in its log) | 149/149 | **agree** |
| `0x16` terminates frames | `[NCRMNT]` (many, not all) | 149/149 | **agree**; the exceptions in its log are its own byte loss |
| Two baud rates per session | `[NCRMNT]` | 2400 → 115200, visible in every erase/flash log | **agree** |
| Host pulses `0x7F` until answered | `[NCRMNT]`, `[EMBDEV]` | not observable (host TX of the pulse is not logged) | **not contradicted**, still `[NEEDS-BENCH]` |
| Link uses even parity | `[NCRMNT]` | not observable from a host-tool log | **open** — §3.2 |
| Cold power-on is the only entry | `[DS89]` §2.2.5, §10.1; `[STC-TOOL]`; `[EMBDEV]` | `00-autoreset-attempt.log` | **agree**, §3.3 |
| BSL waits "tens to hundreds of ms" | `[DS89]` §10.1; `[STC-TOOL]` | not timed | **agree**, unquantified |

One incidental vendor fact worth recording because it explains the
preamble: `[DS89]`'s IAP example code arms every flash operation by
writing `46H` then `0B9H` to `IAP_TRIG`. STC uses the same two bytes as
the "this is really a flash command" token in the *hardware* IAP
interface and as the serial framing magic. Not needed to implement
anything; useful for recognizing that `46 B9` is an STC house pattern
rather than a coincidence.

---

## 3. Physical layer

### 3.1 Wiring and pins

- The download interface is **`P3.0`/`P3.1` only** `[STC-TOOL]`,
  `[DS89]` §10.2. `[DS89]` §10.3 states the connections explicitly: MCU
  RxD (P3.0) to the PC's TxD, MCU TxD (P3.1) to the PC's RxD, and common
  ground. A USB-TTL adapter must therefore be **crossed**.
- This collides with `src/10-live-firmware`, which also wants P3.0/P3.1 —
  a documented conflict (`CLAUDE.md`), and the direct cause of the noise
  in §5.1.2.

### 3.2 UART parameters

| Parameter | Value | Basis |
|---|---|---|
| Handshake baud | **2400** | stcgal's default per `NOTES.md`, and independently confirmed by §7.1: the frequency relation only yields a clean constant if the handshake rate is 2400 |
| Transfer baud | **115200** | the tool's own message and the reload byte `0xFD` decoded in §7.2 |
| Data bits | 8 | assumed; not observable from a host-tool log |
| Parity | **open** | `[NCRMNT]` claims even parity for its generation. `[NEEDS-BENCH]` |
| Stop bits | 1 | assumed. `[NEEDS-BENCH]` |
| Flow control | none | no evidence of any |

`[NEEDS-BENCH]` **Wire framing.** *Experiment:* put a logic analyzer (or
a second adapter) on P3.0/P3.1 during a known-good session and decode the
line at 8N1 and 8E1; the framing that yields the `46 B9 … 16` structure
byte-for-byte is the answer. Until then `stcbsl` should make parity a
constant in one place, because a wrong choice will present as "the chip
never answers" rather than as an error.

### 3.3 The entry condition — cold power-on, and nothing else

Vendor-stated and bench-confirmed, and the fact this repository has been
shouting since its first commit.

`[DS89]` §2.2.5 gives the authoritative table:

| Reset type | Source | Result |
|---|---|---|
| Warm boot | watchdog, reset pin | resets to **AP** address 0000H — runs the user application |
| Warm boot | writing `20H` or `60H` to `ISP_CONTR` | resets to **ISP** address 0000H — runs the BSL |
| Cold boot | power-on | resets to **ISP** address 0000H; if no legitimate ISP command is detected, software-resets into the user program automatically |

`[DS89]` §10.1 says it again in one sentence: it must be a cold reset,
and *for any warm reset (including reset-pin and watchdog) the MCU runs
user code directly*.

**Bench confirmation — the DTR finding.** `00-autoreset-attempt.log` is
the whole experiment: the host toggled DTR ("Cycling power: done"), then
waited ("Waiting for MCU:"), and **not one byte ever arrived** — the log
has no packet lines at all. Two readings, and both lead to the same
place: either DTR does not reach this board's power rail (`NOTES.md`'s
reading — the board has a manual power switch), or it reaches RST, in
which case `[DS89]` §2.2.5 says a reset-pin reset is a *warm* boot and
goes to the application. There is no wiring of DTR that produces a cold
boot on its own.

Consequences a host implementation must respect:

1. `stcbsl` **must not offer an autoreset mode** for this part without
   evidence that the specific board wires DTR to the power rail. Offering
   one that silently does nothing is worse than not offering it.
2. There is a **race, not a handshake**, at the start: the BSL listens
   for "tens of milliseconds to several hundred milliseconds" `[DS89]`
   §10.1, `[STC-TOOL]` and then jumps to user code. The host must
   therefore already be transmitting *before* power is applied
   (`[DS89]` §10.3: press download first, then power on; `[GRAUONLINE]`,
   `[EMBDEV]` say the same).
3. The **retry unit is a power cycle**, not a resend. Every failure path
   in §6 ends there.

Two documented bypasses, neither part of v1:

- A running user program can write `20H`/`60H` to `ISP_CONTR` and reboot
  itself into the BSL `[DS89]` §2.2.5. This is a firmware-side trick —
  a cooperating application could give us push-button reflashing — but it
  is not something the host can command over the wire on a chip that is
  already running arbitrary code.
- **Programming protection pins.** On this generation an option can
  require **P1.0/P1.1 to be pulled to GND** before download is permitted
  `[DS89]` §10.1 (P3.2/P3.3 on STC15/STC8 `[STC-TOOL]`). Our chip has
  `bsl_pindetect` off, so this was never exercised; a chip with it on
  looks bricked to a host that does not know about it.

### 3.4 Two baud rates per session

Sessions run the handshake at 2400 and, **only if there is work to do**,
switch to 115200 for the rest. The switch is a three-step negotiation
(§5.3), not a unilateral change.

An info-only session **never switches**: `01-info-run1.log` is two frames
long — status in, `0x82` out — both at 2400. So the baud switch belongs
to the work, not to the session.

The switch must happen **on the open port**. `docs/BENCH-FLASHING.md`
already records that a close-and-reopen loses bytes across the gap; the
protocol gives both ends a fixed moment to retune (§5.3) and nothing to
resynchronize with afterwards except the `46 B9` preamble.

### 3.5 Data rate, and what it implies for timeouts

The captures carry no timestamps, so these are throughput figures the
capturing tool reported, not measurements of individual operations.

- Programming ran at **~4.1 KB/s** (`03-flash-blink-run1`, 512 bytes) and
  **~3.7 KB/s** (`04-flash-hello-run2`, 1152 bytes) at 115200.
- A 142-byte write frame is ~12–14 ms of line time at 115200 depending on
  parity, and its 9-byte ack is under 1 ms. At ~31 ms per 128-byte block
  round trip, roughly **15–20 ms per block is the MCU actually
  programming**.
- After the desync in `03-flash-blink-run2` the reported rate collapsed to
  231 B/s, which is the host's read timeout dominating — useful as a
  smell test, not as a number.

`[NEEDS-BENCH]` **Real timings.** *Experiment:* re-capture with
per-line timestamps (`ts` from `moreutils`, or a timestamping tap) and
read off: BSL detection window, erase duration, per-block program time,
and the host's own timeout. Until then `stcbsl` should use generous
per-command timeouts (order 1 s) and a much longer one for erase.

---

## 4. Frame grammar

**Derived from 149 frames across nine sessions; all 149 satisfy every
rule below with zero exceptions.** Re-check it with
`tools/isp-capture/normalize.py … --strict`, which implements exactly
this grammar.

```
  +------+------+-----+-----------+-----+---------------+------+------+
  | 0x46 | 0xB9 | DIR | LEN (BE16)| CMD |    PAYLOAD    | CKSM | 0x16 |
  +------+------+-----+-----------+-----+---------------+------+------+
     0      1      2      3   4      5     6 … n-3        n-2    n-1
```

| Field | Size | Rule |
|---|---|---|
| magic | 2 | always `46 B9`. |
| `DIR` | 1 | `0x68` in MCU→host frames, `0x6A` in host→MCU frames. 149/149. |
| `LEN` | 2, big-endian | **`LEN = n − 2`**, i.e. it counts every byte after the magic: `DIR`, the length field itself, `CMD`, payload, checksum and terminator. 149/149. |
| `CMD` | 1 | command/response selector, §5. Formally the first payload byte; separated here because every frame has one. |
| payload | 0 … 133 | command-specific, §5. Largest observed frame `n = 142`. |
| `CKSM` | 1 | **`CKSM = (Σ frame[2 … n−3]) mod 256`** — the 8-bit sum of everything from `DIR` up to and including the last payload byte, excluding the magic, the checksum itself and the terminator. 149/149. |
| terminator | 1 | always `0x16`. |

Worked example, the shortest frame in the corpus
(`46 B9 6A 00 06 82 F2 16`, [01-info-run1:2]):

```
LEN  = 0x0006 = 6 = 8 − 2                       ✓
CKSM = 0x6A + 0x00 + 0x06 + 0x82 = 0x0F2 → 0xF2 ✓
```

Two consequences worth spelling out because they show up in the
implementation:

- A request and its echo differ in checksum by exactly **2** when the
  payload is identical, because only `DIR` changed (`0x6A` → `0x68`).
  Visible in the `0x8F`, `0x8E` and `0x8D` exchanges: `28`/`26`,
  `A5`/`A3`, `FB`/`F9`.
- The length field is redundant with the terminator, and the protocol
  has **no escaping**: our write payloads contain arbitrary machine code
  and none of it was stuffed. A receiver must therefore use `LEN` to
  find the frame end, never scan for `0x16`.

`[NEEDS-BENCH]` **`0x46 0xB9` inside a payload.** No captured payload
happened to contain the magic. *Experiment:* build a test image whose
machine code contains the byte pair `46 B9` and flash it; confirm the
frame is unescaped and that the ack checksum still matches.

`[NEEDS-BENCH]` **Maximum frame size.** All write frames are exactly
`n = 142`. *Experiment:* nothing in the protocol suggests the host may
choose a different block size, but a host-side experiment sending 256
data bytes would settle whether the BSL's buffer is fixed at 128.

`[NEEDS-BENCH]` **Error frames.** The MCU never rejected anything in
these captures. *Experiment:* send a frame with a deliberately wrong
checksum and record what comes back (or that nothing does).

---

## 5. Session phases (STC89)

Command bytes, all confirmed across all sessions. The MCU **echoes** the
command byte for `8F`, `8E` and `8D`, and answers **`0x80`** for
everything else it answers at all.

| `CMD` | Direction | Phase | Reply |
|---|---|---|---|
| `0x00` | MCU→host | status / identify (§5.2) | — (unsolicited) |
| `0x8F` | host→MCU | baud probe (§5.3) | echo `0x8F`, same payload |
| `0x8E` | host→MCU | baud commit (§5.3) | echo `0x8E`, same payload |
| `0x80` | host→MCU | link test (§5.4) | `0x80`, empty payload |
| `0x84` | host→MCU | erase (§5.5) | `0x80`, 7-byte payload |
| `0x00` | host→MCU | write block (§5.6) | `0x80`, 1-byte payload |
| `0x8D` | host→MCU | options (§5.7) | echo `0x8D`, same payload |
| `0x82` | host→MCU | run application (§5.8) | none |

### 5.1 Sync

#### 5.1.1 The pulse train

The host sends a stream of `0x7F` bytes at the handshake baud until the
MCU answers `[NCRMNT]`, `[EMBDEV]`; `[EMBDEV]` describes the mechanism
from the chip's side as watching P3.0 for a falling edge during the
post-power-on window. `0x7F` is `0b01111111`: sent LSB-first it is a
start bit, seven ones and a zero — one clean isolated edge pair per byte,
which is what makes it usable as a bit-time reference, and §7.1 shows the
MCU does exactly that.

**Our captures do not show this.** They are host-tool logs and the tool
does not dump its own sync bytes. This is the only part of the session
still resting entirely on public sources.

`[NEEDS-BENCH]` **The pulse train.** *Experiment:* logic-analyzer capture
of P3.0 during the first two seconds after power-on, decoded as UART at
2400 — recovers the byte value, the cadence, any inter-byte gap, and how
many bytes precede the MCU's answer. `tools/isp-capture/normalize.py`
already has a stub format for the analyzer export.

#### 5.1.2 Noise rejection — a hard requirement, not a nicety

`05-timeout-nocycle.log` was recorded as a timeout fixture and turned out
to be better than designed. The chip was still **running the previously
flashed `04-hello89` firmware**, which prints on the same UART. So while
the host waited at 2400 baud for a bootloader, the line was carrying an
application's 115200-baud output, misframed into garbage.

The evidence: `05-timeout-nocycle` shows **152 single-byte receptions**
(24 distinct values in that one session; **38 distinct** across all 248
noise bytes in the corpus), arriving in a repeating five-byte rhythm
(`a0 b3 e1 25 fc`) for 30 seconds. Every capture that involved a
previously flashed chip shows the same thing before the handshake —
`04-flash-hello-run2.log` opens with 31 such bytes, `02-erase-run1.log`
with 20.

Crucially: **none of the 248 noise bytes across the whole corpus is
`0x46` or `0xB9`.** That is luck, not design, and an implementation must
not depend on it.

The requirement this creates:

> The host's receiver must **hunt for the `46 B9` preamble** and discard
> anything that is not part of a frame, throughout the wait. Not "tolerate
> silence" — actively reject bytes. A receiver that assumes the first
> byte it sees is the start of a frame will fail on any board where the
> application talks on the ISP UART, which is most boards, including
> every one this repo owns.

The corresponding failure is captured too: in `03-flash-blink-run2.log`
a single stray `0x00` arrived mid-programming where a frame header was
expected [03-flash-blink-run2:41], the host reported a frame-start error
and aborted **with the flash half-written (128 of 512 bytes)**. A
resynchronizing receiver would have discarded that byte and read the ack
that followed it.

`stcbsl` should:
- discard bytes until `46 B9`, at every read, in every phase;
- validate `LEN` and `CKSM` and re-hunt on failure rather than abort;
- report a mid-programming abort as **"flash left in an indeterminate
  state, power-cycle and reflash"**, because that is what it is.

### 5.2 Status packet (`CMD 0x00`, MCU→host, 60 bytes)

Sent **unprompted** the moment the handshake succeeds; the host does not
ask for it and does not acknowledge it. Identical layout in all eight
sessions that got that far.

```
off  0  1   2   3  4   5   6 ......... 21   22 23  24   25 26  27   28 ....... 56   57   58   59
    46 B9  68  00 3A  00  <8×16-bit words>  66 43  FD  F0 02  82  <29 opaque>  04  CKSM  16
                            frequency        ver  opt  chip
```

| Offset | Content | Evidence |
|---|---|---|
| 6…21 | **eight repeats of the same 16-bit big-endian word** — the frequency measurement, §7.1. `0A 6B` (2667) or `0A 79` (2681) in our captures | all 8 status packets; the eight words are always equal to each other within a packet |
| 22…23 | **BSL version**, `66 43`. `0x66` is BCD `6.6`; `0x43` is ASCII `'C'` — together the "6.6C" the tool reports | constant across all 8 |
| 24 | **option byte**, `0xFD` — the same byte the host writes back in the `0x8D` frame (§5.7) | constant across all 8; equals `0x8D` payload byte 0 in every flash session |
| 25…26 | **chip identity**, `F0 02` — the "Magic F002" the tool maps to STC89C52RC/LE52RC | constant across all 8 |
| 27 | `0x82`, constant, meaning unknown | constant across all 8 |
| 28…56 | **opaque and session-varying** — see below | varies run to run on the same chip |
| 57 | `0x04`, constant, meaning unknown | constant across all 8 |

**The opaque region is a trap and must be left alone.** Offsets 28…56
differ between two runs taken minutes apart on the same chip with the
same firmware: 6 bytes differ between the two info runs, 4 between the
two erase runs, 23 between the two blink runs. These are not reception
errors — every one of those packets passes the §4 checksum, and a
corrupted byte would break it. The MCU genuinely sends different values.
An implementation must therefore **not** validate, fingerprint, or
identify on anything in 28…56.

`[NEEDS-BENCH]` **What the opaque region is.** *Experiment:* capture ten
consecutive sessions without touching the board, and ten more at a
different handshake baud; if the values track the handshake they are
further measurements, if they drift with time they are uninitialized RAM.
Either answer is actionable; today the honest answer is "unknown".

`[NEEDS-BENCH]` **Offset 55 is also `0xFD`**, the same value as the
option byte at 24. *Experiment:* change one option and see whether both
offsets move.

`[NEEDS-BENCH]` **Chip-identity encoding.** One data point:
`F0 02` ↔ STC89C52RC, 8 KB. `[DS89]` §1.9's naming rules make "52" mean
8 KB of program space, which is *consistent* with a low byte that indexes
the program-space code point, but a single sample cannot establish that
and the datasheet contains no device-ID table. *Experiment:* capture an
STC89C51RC (4 KB) and compare the low byte.

### 5.3 Baud negotiation

Three steps, in this order, and only in sessions that have work to do
(§3.4). All bytes below are from [02-erase-run1:38-41], and are identical
in all six sessions that switch.

**Step 1 — probe (`0x8F`).** Host, at the *handshake* baud:

```
46 B9 6A 00 0C 8F FF FD 00 06 A0 81 28 16
                  \___/ \___/ \/ \/
                    |     |    |  +-- 0x81, present only in the probe
                    |     |    +----- 0xA0, constant
                    |     +---------- 0x0006, constant
                    +---------------- FFFD, the timer reload (§7.2)
```

The MCU echoes the frame with `DIR` swapped and the payload unchanged.

**Step 2 — commit (`0x8E`).** The same payload **minus the trailing
`0x81`**:

```
46 B9 6A 00 0B 8E FF FD 00 06 A0 A5 16
```

Again echoed unchanged. Both ends retune after this exchange.

**Step 3 — link test (`0x80`).** §5.4, at the new baud.

`[NEEDS-BENCH]` **The constants `00 06`, `A0`, and the probe-only
`0x81`.** They never varied because every capture used the same pair of
baud rates. *Experiment:* capture two more sessions, one with a different
transfer baud and one with a different handshake baud, and diff. §7.2
predicts the reload byte becomes `0xEE` for a 19200 transfer baud, which
also serves as a check that the reload derivation is right.

`[NEEDS-BENCH]` **Failure behaviour.** The negotiation never failed in
these captures. *Experiment:* propose an unreachable rate and see whether
the MCU stays at the handshake baud or the session dies.

### 5.4 Link test (`CMD 0x80`, host→MCU)

Sent **four times, byte-identical**, immediately after the baud commit,
each answered by an empty `0x80`:

```
host: 46 B9 6A 00 0C 80 00 00 36 01 F0 02 1F 16
mcu:  46 B9 68 00 06 80 EE 16
```

Note `F0 02` — the chip identity again, echoed back at the host. The four
repeats are identical in payload and in the MCU's reply, in all six
sessions.

An implementation should send all four and require all four acks: they
are the only proof that both ends actually landed on the new baud, and
the cheapest possible place to discover that they did not.

`[NEEDS-BENCH]` **Payload meaning and repeat count.** `00 00 36 01 F0 02`
never varied. *Experiment:* vary the image size and the target baud and
diff; if nothing moves, the payload is a fixed challenge and the count is
a policy the host chooses.

### 5.5 Erase (`CMD 0x84`, host→MCU)

```
host: 46 B9 6A 00 0D 84 NN 33 33 33 33 33 33 CKSM 16
mcu:  46 B9 68 00 0D 80 F0 02 C4 2B 01 83 74 CE 16
```

`NN` is a **block count**, and one block is **256 bytes**. Three
independent values across the corpus, and each is confirmed twice:

| Session | Image | `NN` | Erased | Bytes subsequently written |
|---|---|---|---|---|
| `02-erase-*` (`-e`, no image) | — | `0x20` = 32 | 8192 B = the whole 8 KB code flash | 0 |
| `03-flash-blink-*` | 299 B | `0x02` = 2 | 512 B | 512 B (4 × 128) |
| `04-flash-hello-*` | 608 B | `0x04` = 4 | 1024 B | 1024 B (8 × 128) |

**The block count exactly determines how much is subsequently written**:
`NN × 256` bytes, every time. So the host's rule is: erase a region, then
fill the whole erased region, padding with `0xFF` (both flash sessions
end with all-`FF` blocks).

Both image cases fit `NN = 2 × ceil(size / 512)`, i.e. the erase
granularity is 512 bytes expressed as an even number of 256-byte blocks,
and the whole-chip case fits it too (`2 × ceil(8192/512) = 32`).

`[RESOLVED — 512-byte rule]` **Erase granularity.** `NN = 2 ×
ceil(size/512)` and `NN = ceil(size/256)` both fit the blink case; a
second, independent image separates them. Bench capture
`06-write-frame-error.log` flashes a **714-byte** image and reports
**"Erasing 4 blocks"**: `2 × ceil(714/512) = 4` (512-byte rule) vs
`ceil(714/256) = 3` (256-byte rule). The reported 4 can only come from
the 512-byte rule, matching the hello case (608 B → 4) from a different
size — so the granularity is **512 bytes, expressed as an even number of
256-byte blocks**, confirmed by two independent samples. `stcbsl`
implements this rule (no `[NEEDS-BENCH]` guard remains on it). The same
`06` log is also a second sighting of the mid-write "incorrect frame
start" desync noted under §5.1.2.

`[NEEDS-BENCH]` **The reply payload `F0 02 C4 2B 01 83 74`** is constant
in all six sessions. `F0 02` is the chip identity; the remaining five
bytes are unexplained. *Experiment:* compare against a different STC89
part.

`[NEEDS-BENCH]` **EEPROM / data flash.** The chip reports 6 KB of EEPROM
flash and none of our captures touched it. *Experiment:* a session with
EEPROM content, and a session with the `eeprom_erase` option enabled.

### 5.6 Write block (`CMD 0x00`, host→MCU)

```
46 B9 6A 00 8C 00 | 00 00 AH AL | 00 80 | <128 data bytes> | CKSM 16
                    address BE     count
```

- **Address**: a 4-byte big-endian field. Only the low 16 bits were ever
  non-zero (`0x0000` … `0x0380`), stepping by `0x80` = 128.
- **Count**: `00 80` = 128 in every frame — redundant with `LEN`, but
  present. It is a length and not an end address: the block at `0x0080`
  carries `00 80` in both fields.
- **Data**: exactly 128 bytes, `0xFF`-padded at the end of the image.
- Frame is always `n = 142`.

**Acknowledgement, and this is the important part:**

```
mcu: 46 B9 68 00 07 80 SS CKSM 16
```

with a **one-byte payload `SS` = (Σ of the block's 128 data bytes) mod
256**. Verified on **21/21 write/ack pairs** across four sessions, e.g.
[03-flash-blink-run1:35-36]: data sums to `0x3B3C`, ack byte is `0x3C`.
An all-`FF` block sums to `0x7F80` and always acks `0x80`.

So the MCU independently checksums what it received and tells the host.
`stcbsl` **must verify it** — it is the only per-block integrity signal
in the protocol, and it is free.

`[NEEDS-BENCH]` **Whether a wrong `SS` means the block was rejected or
merely reported.** *Experiment:* corrupt one data byte in transit (send a
frame whose checksum is valid but whose data differs from what was
intended) and see whether the MCU still programs it.

`[NEEDS-BENCH]` **Read-back.** No read command appears anywhere in the
corpus, and STC is known to protect code from being read out. **Assume
verification by read-back is impossible** and do not design a
`--verify` flag around it. *Experiment:* try the unused command space
(`0x81`, `0x83`, `0x85`, `0x8A`…) and record what answers — but do it on
a chip we are willing to lose.

`[NEEDS-BENCH]` **Addresses above 64 KB** — this part has 8 KB.
*Experiment:* an STC89C58RD+ or larger.

### 5.7 Options (`CMD 0x8D`, host→MCU)

```
host: 46 B9 6A 00 0A 8D FD FF FF FF FB 16
mcu:  46 B9 68 00 0A 8D FD FF FF FF F9 16    (echo)
```

Payload byte 0 is the **option byte**, and it is exactly the value the
status packet reported at offset 24 — the host read the current options
and wrote them straight back, which is why the two match. The three
trailing `FF`s never varied.

Sent **only in the flashing sessions**; the erase-only sessions go
straight from erase to `0x82`. So the options write is optional, and a
host that has nothing to change may skip it.

The **seven option semantics are vendor-documented features** — 6T/12T
core divider `[DS89]` §1.9, P1.0/P1.1 download protection `[DS89]` §10.1,
ALE/P4.5 `[DS89]` §1.10, auxiliary-RAM enable `[DS89]` §3, watchdog
behaviour `[DS89]` §2.2 — and `[DS89]` §10.3's screenshot of the vendor
tool shows them as a checkbox list. What is **not** documented anywhere,
and not derivable from a single observed value, is **which bit is which**.

`[NEEDS-BENCH]` **The option-byte bit map — highest-value open item.**
*Experiment:* six or seven short sessions, each flipping exactly one
option from its current state, capturing the resulting `0x8D` payload and
the following session's status offset 24. One bit moves per run; the map
falls out. Do it on a chip we can afford to lose, and **never enable the
P1.0/P1.1 download protection while probing** — of this part's seven
options it is the one that can make a board unflashable without a wiring
change (`[DS89]` §10.1: with it set, P1.0 and P1.1 must be pulled to GND
before any future download).

Until that map exists, `stcbsl` **must refuse to write options at all**
except by echoing back the byte it read. Writing an invented default here
is the one operation in this protocol that can cost hardware.

### 5.8 Run application (`CMD 0x82`, host→MCU)

```
46 B9 6A 00 06 82 F2 16
```

The shortest frame in the protocol: command only, no payload. **The MCU
never answers it** — every session ends with this frame and then silence.
A host that waits for an ack here will report a false failure on a
perfectly successful flash.

It is sent at whatever baud the session is currently using: 2400 in the
info-only sessions, 115200 in every other, including the aborted
`03-flash-blink-run2`, where the host sent it after the protocol error to
let the (half-programmed) chip go.

---

## 6. Host state machine

Confirmed against all nine captures; the two dashed paths are the two
failure fixtures.

```
   open port @ 2400, start pulsing 0x7F
                |
      [SYNC] ---+--- bytes arrive that are not 46 B9 ---> DISCARD, keep hunting
         |                                    (05-timeout-nocycle: 152 of them)
         |  46 B9 68 … CMD=00
         v
    [STATUS]  parse: 8×freq word, version, option byte, chip id
         |    (offsets 28…56 are opaque — do not touch)
         |
         +---- nothing to do? --> [RUN] at 2400            (01-info-*)
         |
         v
  [BAUD PROBE]  -> 8F {reload, consts, 81}   <- echo
         v
  [BAUD COMMIT] -> 8E {reload, consts}       <- echo
         |        both ends retune, in place, no close/reopen
         v
   [LINK TEST]  -> 80 {…} <- 80 {}   ×4, all four must ack
         |
         v
     [ERASE]    -> 84 {NN blocks}  <- 80 {7 bytes}     long timeout
         |
         v
     [WRITE]    -> 00 {addr, 128, data}  <- 80 {sum&0xFF}
         |         repeat NN×256/128 times, 0xFF-padded
         |         verify every ack byte  ------ mismatch --> ABORT (indeterminate flash)
         v
   [OPTIONS]    -> 8D {option byte}  <- echo        (skippable)
         |
         v
      [RUN]     -> 82                 no reply expected, ever
```

Failure handling, from the evidence:

- **Any desync is recoverable only by re-hunting for `46 B9`** (§5.1.2).
  `03-flash-blink-run2` shows what happens otherwise: one stray `0x00`
  ended the session at 25% written.
- **Any failure after `[SYNC]` requires a power cycle to retry**, because
  the BSL is gone once it hands over to user code (§3.3). The CLI must
  say so out loud, every time.
- **A half-written flash is not a recoverable state.** There is no
  read-back to check against (§5.6), so the only honest report is
  "indeterminate, reflash".
- **No retry was ever observed** in these captures, so the protocol's own
  retry policy is unknown; a host-side retry is a host-side choice.
  `[NEEDS-BENCH]`.

---

## 7. The frequency handling (STC89)

The single most consequential finding in this capture set, and the one an
implementation is most likely to get wrong by assuming.

### 7.1 The target frequency is MEASURED, not a constant

The same board, the same crystal, sessions minutes apart, reported
**10.973 MHz** in three sessions and **11.030 MHz** in five. That is not
bench sloppiness — it is what the protocol does.

The status packet's eight repeated words are the raw measurement, and the
frequency follows from them exactly:

```
  f_osc  =  word  ×  baud_handshake  ×  12 / 7
```

| Word | Decimal | Computed | Reported |
|---|---|---|---|
| `0x0A6B` | 2667 | 2667 × 2400 × 12/7 = 10 972 800 Hz | 10.973 MHz |
| `0x0A79` | 2681 | 2681 × 2400 × 12/7 = 11 030 400 Hz | 11.030 MHz |

Both to five significant figures, on all eight status packets. The
`12/7` falls out of the division and is exact: 10 972 800 / (2667 × 2400)
= 1.714285… = 12/7. The `12` is the classic 8051 machine cycle; the `7`
is the number of bit times the BSL evidently measures across a `0x7F`
byte. Note that this arithmetic only closes if the handshake baud is
2400 — which is independent confirmation of §3.2's first row.

The board's crystal is nominally 11.0592 MHz. The measurements land
0.3 % and 0.8 % **low**, and vary between runs on the same hardware, so
the word is a genuine per-handshake measurement of the host's bit time
against the MCU's clock, not a stored constant.

What a host must therefore do:

1. **Never assume a clock.** Read the word from the status packet.
2. **Derive the transfer baud's timer reload from the measured value**,
   not from a nominal crystal frequency — §7.2. Using 11.0592 MHz where
   the chip measured 10.9728 MHz is a 0.8 % error, which is survivable at
   115200 but is exactly the kind of margin that turns a marginal adapter
   into an intermittent failure.
3. **Do not report the measured frequency as the crystal frequency**
   without saying it was measured. It is not a property of the board; it
   is a property of the handshake.

Sanity checks worth implementing: all eight words should be equal
(they always were), and the derived frequency should be inside the part's
rated range before it is used for anything.

### 7.2 What the host computes from it

The `0x8F`/`0x8E` payloads carry the value `FF FD` for a 115200-baud
target. This is the **UART timer reload**, 16-bit big-endian two's
complement (`0xFFFD` = −3, i.e. an 8-bit reload of `0xFD` = 253):

```
  reload = round( 256 − f_osc / (baud_transfer × 32) )
```

| f_osc | target baud | computed | byte |
|---|---|---|---|
| 10 972 800 | 115200 | 253.023 | `0xFD` ✓ observed |
| 11 030 400 | 115200 | 253.008 | `0xFD` ✓ observed |
| 11 030 400 | 19200 | 238.047 | `0xEE` — the prediction to test |
| 11 030 400 | 9600 | 220.094 | `0xDC` |

Both measured frequencies round to the same byte at 115200, so the
captures **confirm the formula is consistent but do not uniquely
determine it** — a 32× or 16× divisor with a different constant could
also fit two samples that land on the same answer.

`[NEEDS-BENCH]` **Pin the reload formula.** *Experiment:* one capture at
a transfer baud of 19200. If the payload carries `FF EE`, the formula
above is right and `stcbsl` can compute reloads for any rate; if it
carries something else, we have two equations and can solve properly.
This is cheap and it unblocks arbitrary baud support.

---

## 8. Determinism: run1 vs run2

Every session was captured twice so that "constant" could be told apart
from "coincidence". Comparing frame-by-frame (noise excluded):

| Session pair | Frames | Identical | What differed |
|---|---|---|---|
| `01-info` | 2 vs 2 | 1/2 | status packet only: offsets 30, 31, 35, 36, 49, 58 |
| `02-erase` | 16 vs 16 | 15/16 | status packet only: offsets 29, 40, 49, 58 |
| `03-flash-blink` | 26 vs 19 | 17/19 | status packet (23 offsets); run2 aborted at the 2nd write |
| `04-flash-hello` | 34 vs 34 | 33/34 | status packet only: offsets 7, 9, …, 21 (frequency), 31, 37, 38, 40, 47, 49, 58 |

Read off that table:

- **Every host frame is byte-identical between runs.** Same image, same
  bytes, in the same order — the host is a pure function of (image,
  status packet). That is what makes replay testing possible at all.
- **Every MCU reply except the status packet is byte-identical between
  runs**: the `8F`/`8E` echoes, the four link-test acks, the erase reply,
  and each write ack (identical because the same image produces the same
  data sums).
- **The status packet is the only non-deterministic frame.** Two kinds of
  variation, and they must be handled differently:
  - **offsets 6…21** — the frequency words. Legitimately session-dependent
    (§7.1). `04-flash-hello` run1 measured `0A6B` and run2 `0A79`, which
    is the finding in one line.
  - **offsets 28…56** — opaque, varying, meaningless to us (§5.2). Never
    validate against them.
  - offset 58 is the checksum, which naturally moves with the payload.
- **The write acks vary with the data, exactly as §5.6 predicts.** The
  all-`0xFF` blocks ack `0x80` in *both* the blink and hello sessions,
  across different images — the strongest single confirmation that the
  ack is a data checksum and not a sequence counter.

A replay test must therefore treat the status packet as an **input**
(feed it from the fixture) and every host frame as an **expected output**
(compare byte-for-byte). The only host-side value derived from the
varying part of the status packet is the reload byte, and at 115200 both
observed frequencies give the same byte — so `04-flash-hello-run1` and
`run2` are a genuine two-frequency test of that path.

---

## 9. Part identity (STC89C52RC)

- Chip identity bytes **`F0 02`**, status offsets 25…26 and echoed in the
  erase reply and link-test payload.
- BSL version **`66 43`** = 6.6C.
- 8 KB code flash = 32 erase blocks of 256 bytes; 6 KB EEPROM flash,
  untouched by these captures.
- `[DS89]` §1.9's naming rules decode the part number itself: `STC89` +
  `C` (5.5–3.3 V) + `52` (8 KB program) + `RC` (512 B RAM), rated to
  40 MHz. Consistent with everything the status packet reports, though
  the datasheet contains **no device-ID table**, so the `F0 02` ↔ part
  mapping rests on our single capture (§5.2, `[NEEDS-BENCH]`).
- `[DS89]` §1.10 splits this family into **90C and HD versions** with
  different pin sets (90C has P4.4/P4.6 and no PSEN/EA; HD has ALE/PSEN/EA
  and no P4.4–P4.6). Nothing in the status packet obviously distinguishes
  them. `[NEEDS-BENCH]`: whether the BSL reports the version at all, or
  whether the vendor tool relies on the user's selection.

---

## 10. STC12 chapter (stub)

Not started. The lab's STC12C5A60S2 is this repo's original target.

**Do not populate this by analogy with §4–§9.** The one structural thing
worth carrying over as a *hypothesis to test* is the `46 B9 … 16` frame,
which `[NCRMNT]` saw on other STC silicon. The checksum width in
particular must be re-derived, not assumed: our STC89 rule is an 8-bit
sum, and nothing establishes that a later generation kept it. An
"obvious" port that is wrong here corrupts flash silently.

Part facts for when that bench session happens: 1T core, dedicated BRT
baud generator, `docs/PINOUT.md`, `docs/STC12-PERIPHERAL-MODEL.md`.

## 11. STC15 chapter (stub)

Not started; `docs/PINOUT-STC15.md`, `docs/STC15-PERIPHERAL-MODEL.md`.

Two things already known to matter:

- No external crystal in the usual layout — the RC oscillator is trimmed,
  so §7.1's measured frequency will vary far more than the 0.8 % we saw
  on a crystal, part to part and with temperature. The measured-not-assumed
  discipline of §7.1 becomes mandatory rather than merely correct.
- UART1 can be remapped off the ISP pins, which is the fix for the
  monitor/ISP contention in `CLAUDE.md`. Whether the BSL honours a remap
  (it should not — it runs before user code) is `[NEEDS-BENCH]`.
- The programming-protection pins are P3.2/P3.3, not P1.0/P1.1
  `[STC-TOOL]`.

---

## 12. Open items — the `[NEEDS-BENCH]` register

Every one has an experiment. Ordered by what blocks the implementation
soonest.

### Blocking a first flash

| # | Item | Experiment | §|
|---|---|---|---|
| B-1 | Wire framing: parity and stop bits | Logic-analyzer decode at 8N1 vs 8E1 during a good session | 3.2 |
| B-2 | The `0x7F` pulse train: value, cadence, count | Logic-analyzer capture of P3.0 for 2 s after power-on | 5.1.1 |
| B-3 | Reload formula pinned | One capture at 19200 transfer baud; predicts payload `FF EE` | 7.2 |

### Blocking correctness on other images or parts

| # | Item | Experiment | § |
|---|---|---|---|
| C-1 | ~~Erase granularity (512 B vs 256 B rule)~~ **RESOLVED**: 512-byte rule, confirmed by `06`'s 714 B → 4 blocks (§5.5) | done | 5.5 |
| C-2 | Magic bytes inside a payload | Flash an image containing the byte pair `46 B9` | 4 |
| C-3 | Whether a bad ack byte means rejection | Send a frame whose data differs from intent | 5.6 |
| C-4 | Address field above 64 KB | Capture an STC89C58RD+ or larger | 5.6 |
| C-5 | Chip-ID encoding | Capture an STC89C51RC (4 KB) and diff the low byte | 5.2 |
| C-6 | Maximum block size | Try a 256-byte write block | 4 |

### Blocking the option-writing feature (do not ship it before this)

| # | Item | Experiment | § |
|---|---|---|---|
| O-1 | Option-byte bit map | One short session per option, flipping exactly one; diff `0x8D` payload and status offset 24 | 5.7 |
| O-2 | Whether offset 55 tracks offset 24 | Same runs as O-1 | 5.2 |
| O-3 | EEPROM / data-flash handling | A session with EEPROM content and with `eeprom_erase` on | 5.5 |

### Would improve the implementation but block nothing

| # | Item | Experiment | § |
|---|---|---|---|
| N-1 | Real timings: BSL window, erase duration, per-block program time | Re-capture with per-line timestamps | 3.5 |
| N-2 | Error frames | Send a deliberately bad checksum | 4 |
| N-3 | Baud-negotiation failure behaviour | Propose an unreachable rate | 5.3 |
| N-4 | Meaning of `00 06 A0` / probe-only `0x81` | Vary both baud rates and diff | 5.3 |
| N-5 | Link-test payload and repeat count | Vary image size and baud; diff | 5.4 |
| N-6 | Erase reply tail `C4 2B 01 83 74` | Compare against another STC89 part | 5.5 |
| N-7 | Status opaque region 28…56 | Ten consecutive sessions, then ten at another handshake baud | 5.2 |
| N-8 | Read-back / unused command space | Probe `0x81`, `0x83`, `0x85`… on an expendable chip | 5.6 |
| N-9 | Protocol-level retry policy | Drop a frame deliberately and watch | 6 |
| N-10 | 90C vs HD version reporting | Capture both variants | 9 |

---

## 13. Change log

| Date | Change |
|---|---|
| 2026-08-17 | Phase 1: skeleton from public sources only; everything `[NEEDS-CAPTURE]`. |
| 2026-08-17 | Phase 2: nine STC89C52RC captures folded in. Frame grammar, checksum, session state machine, frequency derivation and determinism analysis are now derived facts; remaining gaps re-tagged `[NEEDS-BENCH]` with experiments. |
