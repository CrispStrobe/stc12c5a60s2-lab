# A2 board support — first-class blocks for the Prechin 普中51 A2

Goal: turn every hand-scanned A2 demo (`pseudocode/14-a2-keyshow.bw`,
`15-a2-matrix.bw`) into first-class dialect PARTs and blocks, so a
learner draws on the matrix, lights the LEDs, reads the keypad and shows
numbers on the 8-digit display **without writing a scan loop**. Hardware
authority: `docs/BOARD-PRECHIN-A2.md` (every pin measured on silicon).

Reference dialect = `../stc-compiler/stc_pseudocode.py` (PARTs +
generateC). `sb3-creator` mirrors it (generateC + blocks); the circuit
designer gets the faces. Parity is tracked in stc-compiler's
`DIVERGENCES.md`.

## The one hard problem: multiplexed displays must scan transparently

The 8×8 matrix and the 8-digit 7-seg are **multiplexed** — only one
row/digit is lit at a time, refreshed fast enough to look steady. The
hand-written demos scan in a `FOREVER` loop, which monopolises the
program. A first-class device cannot: the user's `WHEN` blocks must keep
running while the display stays lit.

**Solution: refresh in the Timer-0 ISR the cooperative scheduler already
runs.** The dialect already ticks Timer-0 at 1 ms for its millisecond
clock. Each display registers a refresh hook: every tick advances ONE
row/digit from a frame buffer in RAM. 8 rows → a full frame every 8 ms =
125 Hz, flicker-free. The drawing verbs just write the frame buffer; the
ISR does the rest. Generated C allocates the buffer, emits the refresh
inside the Timer-0 ISR, and the verbs become buffer writes — no user
scan loop, no blocking.

This is a `generateC` change (a per-display ISR refresh hook + frame
buffer), the natural extension of the existing PART model. It is the
crux; everything below is vocabulary over it.

## Capability set and block vocabulary

### 8×8 dot matrix — `MATRIX8X8`
Declared over the measured A2 wiring (595 rows active-high Q7=top, port
columns active-low bit7=left; both encoded in the PART so images read
top-down/MSB-left like the literals):

```
PART screen = MATRIX8X8 ROWS <595 data clock latch> COLUMNS <port>
```
Blocks (all operate on the frame buffer; the ISR scans it):
- `clear screen`
- `light pixel X Y` / `clear pixel X Y`  (0..7, origin top-left)
- `set pixel X Y to on|off`
- `draw row Y = <byte>`  (a whole row at once)
- `show image <table> on screen`  (blit 8 bytes — the heart demo, one line)
- `scroll screen left|right|up|down`
- reporter `pixel X Y is on`  (boolean)

### 8 LEDs — `LEDBANK8`
The A2's 8 LEDs share P2 with the 7-seg select (a real, documented
conflict). **Decision: model the shared port honestly — allow the LED
bank and the display in the same program, emit a compile WARNING, and
document that scanning the tubes flickers the LEDs** (the vendor's
"modules can't all be used at once", as measured copper — do not hide
it behind a hard error).
```
PART leds = LEDBANK8 ON <port> [ACTIVE LOW]
```
- `turn on led N` / `turn off led N`  (0..7)
- `set leds to <pattern byte>`
- `light only led N`  (one-hot)
A plain `PORT leds = P2` already does `set leds to <byte>`; LEDBANK8 adds
per-LED addressing and the shared-port warning.

### 4×4 keypad — `KEYPAD4X4` (exists) + sugar + event hats
`PART keys = KEYPAD4X4 ROWS ... COLS ...` already returns the scanned key
`0..15` or `-1`. Add both reporter sugar AND edge-triggered event hats:
- `key`  (0..15, or -1) — exists
- `a key is pressed`  (boolean, `key >= 0`)
- `key N is pressed`  (boolean, one specific key held)
- **`WHEN key N pressed`** — an edge-triggered hat: fires once on the
  press transition, not while held. Needs a small debounced last-key
  state polled by the scheduler (a scanned key must be stable across two
  reads before it counts as a press), and per-key rising-edge detection.
  This is the one piece needing new scheduler infrastructure.

### 8-digit 7-seg display — `SEVENSEG8`
2×4 common-cathode tubes: segments on a port via 74HC245, digit select
via 74HC138 (3 address pins), digit 0 = all select low:
```
PART display = SEVENSEG8 SEGMENTS <port> SELECT <A B C> [COMMON CATHODE|ANODE]
```
- `show number N on display`  (integer, right-aligned; the ISR scans all 8)
- `show digit D = value V on display`  (one tube, 0..7 left-to-right)
- `set digit D to segments <byte>`  (raw)
- `clear display`
Font table is built in (0-9, A-F); the ISR multiplexes the 8 digits.

## Build plan (parity-first) — MATRIX FIRST, then fan out

**Decision: prove the Timer-0-ISR scan pattern once, on the matrix, end
to end (dialect → sdcc → emu8051 → board face, absolute assertions),
BEFORE the other multiplexed device (the 8-digit display) copies it.**
That keeps the scan infrastructure from diverging between devices before
it is proven a single time.

1. **stc-compiler (reference), the matrix as the pattern:** the
   Timer-0-ISR refresh hook + frame buffer, then `MATRIX8X8` and its
   drawing verbs, with the verified C it emits and a compile test.
   `DIVERGENCES.md` updated. (Coordinate with stc-e1: the dialect is its
   oracle; the ISR-scan hook touches the shared scheduler.)
2. **sb3-creator (mirror the matrix):** matching generateC + the matrix
   device blocks; chain-tested compile → emu8051 → board face.
3. **THEN fan out (fleet, in parallel, from this doc + the proven
   pattern):** `SEVENSEG8` (copies the ISR scan), `LEDBANK8` (+ shared-port
   warning), the keypad reporters + `WHEN key N pressed` edge hats, and the
   circuit-designer faces for each.
4. **examples:** rewrite 14/15 as the clean block form, add an A2 sampler
   (keypad → number on the display, a matrix animation, an LED chase).

The A2 board preset (ROADMAP "MAXIMAL BOARD") seats all of these on one
designer board with the measured port-sharing conflicts modeled.
