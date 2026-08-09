# 4x4x4 bi-colour LED cube — measured, not assumed

The kit is the ICStation 4682 / HackerBox 005 cube (the vendor archive is `4681.zip` — product SKU and file id are two numbering systems for the same kit): an STC12C5A60S2 driving 64
LEDs. This directory is the start of turning it into something you can author
animations for from blocks, and it begins where it has to — with what the
hardware actually does, measured under the simulator rather than read off a
forum post.

## Provenance

[`rgm3/ledcube444`](https://github.com/rgm3/ledcube444) carries an **MIT** licence
(rgm, 2016), but it is the vendor's Keil sources reworked to build on SDCC — and
the vendor's own `4681.zip` package is unlicensed. **A downstream MIT grant does
not launder an upstream that had no licence to give**, so rgm3 is not treated
here as independently licensed. That package stays out of this repo either way
(see the note in `CLAUDE.md`).

What lets the topic come back is therefore not rgm3's licence. It is that
everything here is ours:

- **Measurements are facts, not expression.** The table below is what the
  hardware *does*, observed by running firmware under an emulator. Timings and
  port sequences are not copyrightable, and recording them is the interoperability
  case, not a derivative work.
- **`probe.c` is ours**, written against the scan model measured below.
- **Any firmware in this directory is clean-room.** The implementation brief is
  `ucsim-stc`'s `spec-updates/008-ledcube-hardware-spec.md`, which describes the
  hardware and explicitly forbids its implementer from reading the icstation
  source, the rgm3 port, or any derivative. The agent that measured the vendor
  firmware wrote the spec; a different one writes the code. That separation is
  what the term means — a spec written by someone who has read the original is
  only clean-room if they are not also the one implementing it.

## What the hardware does

Run under `emu8051-stc` at FOSC = 11.0592 MHz, the vendor firmware shows its
hand immediately:

| | |
|---|---|
| select lines | `P2`, **active low**, cycling `FE FD FB F7 EF DF BF 7F` |
| data polarity | `P0` **active high** — `1` lights an LED (measured, see below) |
| data | `P0`, 8 bits, written *after* the select |
| per-line dwell | **1.235 ms** — but see below |
| frame | 8 lines = **9.88 ms**, so the cube refreshes at **101 Hz** |
| frame format | 8 bytes: one `P0` byte per select |

**The refresh rate is a property of the build, not of the cube.** The dwell is a software delay
loop, so it moves with the compiler. `ucsim-stc`'s `RESULTS.md` (`9c0ef85`, 5-second windows over
the scan phase) measures the same sources at:

| build | per line | frame | refresh |
|---|---|---|---|
| SDCC | 1.237 ms | 9.895 ms | **101 Hz** |
| Keil | 0.824 ms | 6.594 ms | **152 Hz** |

The SDCC figure agrees with the 1.235 ms above to 0.16%, from two independent measurements — so
that number is solid. The Keil build is **half again faster**, on identical sources.

*(An earlier round reported 1.110 ms/113 Hz for SDCC and 1.527 ms/82 Hz for Keil, and a "27%
spread with Keil slower". All four figures were artifacts of a 50 ms sampling window that
overlapped the firmware's all-on phase, where `P2 = 0x00` and the scan stops. They are retracted
at the source; the direction was wrong, not just the magnitude.)*

So do not treat any of these as a hardware constant. What is fixed is the *constraint*: the full
scan has to stay above ~100 Hz or the cube visibly flickers, and each line needs roughly a
millisecond to be bright enough. That is how
`ucsim-stc/spec-updates/008-ledcube-hardware-spec.md` states it, and it is the right way — an
implementation that hits 124 Hz is not wrong for missing 101.

⚠ **The `P0` row above said active-low, and it was wrong.** The paragraph below calls `P0 = 0`
"blank", which under active-low would light every LED in the selected layer rather than blank it;
the `probe.c` / `main.c` cross-check reached the same contradiction independently. It is now
settled by measurement rather than by argument — `emu8051-stc` Finding #14, a `P0` value
histogram over 5 seconds of the vendor firmware:

| role | value | count |
|---|---|---|
| blank, always *before* a select | `0x00` | 1560 |
| data, always *after* a select | `0xFF` (all on) | 414 |
| data, red columns | `0x0F` | 540 |
| data, blue columns | `0xF0` | 460 |

**Zero exceptions in 3,930+ writes**: `0x00` is never used as data, `0xFF` never as blank. The
semantic cross-check agrees — under active-high `0x0F` means red-on/blue-off, which matches the
red-only layer sweep; under active-low it would mean the opposite of what that animation shows.

**`P0` is active-HIGH: `1` lights an LED.** All four codebases now default
`BW_CUBE_ACTIVE_HIGH = 1`. What remains open is not the firmware's intent but whether the
*hardware* matches it, and only `probe.c` on a real cube answers that — so treat this as measured
and strong, not as confirmed on silicon.

The blank-then-select-then-drive order matters and the firmware honours it:
`P0 = 0; P2 = select; P0 = data;`. Skip the blanking and the previous step's
data ghosts onto this step's LEDs while the select settles.

An animation, in this firmware, is just `uint8_t frames[N][8]` played by one
common scan loop. That is a good shape to compile *to* — which is what makes a
block front end plausible rather than fanciful.

## Two findings worth writing down

**It no longer builds.** SDCC 4.5 rejects K&R empty parameter lists, and the
2016 sources are full of them: 43 functions declared `void f()` where the
compiler now wants `void f(void)`. One word each, no other changes, and it
compiles to a 26 KB hex. Anyone following the upstream README today hits this
on the first `make`.

**Its delays are ~8x short on this chip.** `delay_ms(ms)` is a software loop —
149 empty iterations per "millisecond" — which is right for a 12T 8051 and
wrong for the 1T STC12 the code targets. Measured: a `delay_ms(10)` in the scan
loop takes 1.235 ms, not 10 ms. The animations therefore run several times
faster than the author's numbers suggest, and the 101 Hz refresh is a
consequence of the bug rather than a design choice — at the intended timing the
cube would refresh at about 12 Hz and flicker visibly. This is the drop-in trap
`README.md` §8.1 describes, in the wild. `probe.c` uses Timer 0 at FOSC/12
instead, which a 12T and a 1T part count identically.

## Clean-room firmware: measured under emu8051-stc

`main.c` in this directory is the clean-room implementation. Built with
`sdcc -mmcs51 --model-small`, run under `emu8051-stc` at FOSC = 11,059,200 Hz.

| | vendor (SDCC build, scan phase) | **clean-room** |
|---|---|---|
| per-line dwell | 0.824 ms (software loop) | **1.006 ms** (Timer 0 polled) |
| frame period | 6.595 ms | **8.061 ms** |
| refresh rate | 151.6 Hz | **124.1 Hz** |
| dwell variation | not measured | **0.018%** (uniform) |
| 1T/12T portable | no (~12x too fast on 1T) | **yes** (Timer 0 at FOSC/12) |
| scan order | FE FD FB F7 EF DF BF 7F | same |
| anti-ghost | blank P0, then select | blank P2 across P0 write |
| ghosting violations | 0 / 4192 selects | 0 / 243 P0 writes |

**Build:** `sdcc -mmcs51 --model-small -o /tmp/cr.ihx stc/src/20-ledcube/main.c`

The key difference is the delay mechanism. The vendor firmware uses a software
busy-wait loop that runs ~8x too fast on the 1T STC12 (the delays were sized
for a 12T part). The clean-room version uses Timer 0 in mode 1 at FOSC/12,
which is portable: a 12T and a 1T part count this timer identically. The 1 ms
dwell comes from `reload = 65536 − 922 = 0xFC66`, not from instruction timing.

Dwell uniformity: all 8 scan lines are within 1,005,484–1,005,665 ns (0.018%
spread). No layer is lit longer than any other. The 181 ns variation is one
timer tick at FOSC/12 (1,085 ns period) — the reload value lands at slightly
different phases depending on when the previous overflow occurred.

**Ghosting invariant:** A layer must never be enabled while `P0` holds
another line's data. Blank the data before selecting, or hold every select
off across the data write — either is sufficient; doing neither lights
the previous line's pattern on the new line for the settle time.

The vendor blanks `P0` first (`P0=0; P2=select; P0=data`), so the layer
sees blank→correct. The clean-room driver holds `P2=0xFF` across the data
write (`P0=data; P2=select; delay; P2=0xFF`), so no layer is on while
`P0` changes. Both satisfy the invariant.

**Measured (clean-room, 4 seconds):** 243 P0 writes, **zero** instances
of a layer being enabled while P0 holds stale data. No ghosting possible.

## What is still unknown: the voxel map

Sixty-four bits and sixty-four positions in a 4x4x4 — but the kit is sold as
bi-colour, which would want a hundred and twenty-eight. So either a colour
costs a select line and only half the cube is addressable per colour, or the
two colours are wired to different halves. The vendor's animation tables cannot
settle this: they were authored by looking at a cube, so they encode the answer
without stating it.

`probe.c` settles it by measurement. It walks all 64 `(select, bit)` pairs one
at a time, ~600 ms each, with a blank gap between, and repeats. Flash it, watch
the cube, fill in the table below. Under the simulator it already behaves:
`P2=FE` with `P0=01`, every other select blank, one voxel per 8 ms frame.

```
  build:  sdcc --iram-size 256 -o build/ src/20-ledcube/probe.c
  flash:  make EXAMPLE=20-ledcube flash        (or stcgal -P stc12 …)
```

| select | bit 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| `FE` | | | | | | | | |
| `FD` | | | | | | | | |
| `FB` | | | | | | | | |
| `F7` | | | | | | | | |
| `EF` | | | | | | | | |
| `DF` | | | | | | | | |
| `BF` | | | | | | | | |
| `7F` | | | | | | | | |

Until that table is filled in from a real cube, any renderer we draw is a guess
with a plausible shape, and every layer above it — the simulated cube, the
blocks that author animations — is wrong in exactly the same way.

## What is still unknown: P0 data polarity

The voxel map is one unmeasured fact.  **P0 data polarity is the second**, and
it is the more dangerous of the two because everything *looks* right without it.

The spec (§2) says active-low: `P0 = 0x00` lights all LEDs.  `probe.c` assumes
active-high: its blank frame is `{0,…}`, its probe step is `1 << bit`, and the
vendor firmware's own `P0 = 0` "blank" only makes sense under active-high.
The cross-check section below has the full analysis.

`main.c` isolates the assumption in **one symbol**: `BW_CUBE_ACTIVE_HIGH`
(currently `1`, matching `probe.c`, `sb3-creator`, and `bw-circuit-ui`).
Setting it to `0` flips the framebuffer helpers and init to active-low — no
other changes needed.  All four components now use the same symbol name and
the same default.

Both unknowns — voxel map and polarity — are settled by the same bench session:
flash `probe.c`, watch the cube, and record whether `(FE, 01)` lights one LED
(active-high) or darkens one in an otherwise-lit layer (active-low).

## What `probe.c` and `main.c` agree and disagree about

Both programs were written from the same hardware understanding by different
agents — `probe.c` by the measurement agent, `main.c` from
`008-ledcube-hardware-spec.md` by a cleanroom agent that did not read the
vendor firmware. A disagreement between them is a spec ambiguity; agreement
is evidence the spec is unambiguous, not evidence it is right about the
hardware. Only the physical probe on a real cube settles the second question.

**Scan table — agree.** Both use `{FE,FD,FB,F7,EF,DF,BF,7F}`, same values,
same order, active-low on P2.

**Timer 0 — agree.** Both use Timer 0 at FOSC/12, polled, mode 1. Reload
values differ by 1 tick (`probe.c`: 0xFC67, `main.c`: 0xFC66 — the two
valid roundings of 921.6; the difference is < 1 µs per ms).

**Anti-ghost — agree on outcome, differ in method.** `main.c` holds
`P2 = 0xFF` across the data write, so no layer is on while P0 changes.
`probe.c` blanks P0 first (`P0 = 0; P2 = select; P0 = data`), so the
layer sees blank→correct. Both satisfy the invariant: a layer is never
enabled while P0 holds another line's data.

**Colour mapping — no disagreement, but no confirmation either.** `main.c`
assigns `P0[3:0]` = red, `P0[7:4]` = blue (from the spec). `probe.c` is
deliberately agnostic — it walks all 64 `(select, bit)` pairs and lets the
observer record what lights up. It is the instrument that will confirm or
refute what `main.c` assumes.

**Select-to-layer mapping — same asymmetry.** `main.c` assigns scan lines
0–3 to layers 0–3 first-colour and 4–7 to layers 0–3 second-colour (from
the spec). `probe.c` does not assign meaning to select indices — again, it
measures rather than assumes.

**P0 data polarity — DISAGREE.** This is the one real conflict.

The spec says active-low: a LOW bit lights the LED (`P0 = 0x00` = all on,
`P0 = 0xFF` = all off). `main.c` follows this: `fb_clear()` sets every byte
to `0xFF`, and `fb_set_red` *clears* bits to turn LEDs on.

`probe.c` treats P0 as active-high: its "blank" frame is `{0,0,0,0,…}`,
which under active-low would light every LED rather than blank the display.
Its probe step is `frame[sel] = (1 << bit)` — under active-low, that turns
OFF one LED and turns ON seven, which is backwards for identifying a single
voxel. Both behaviours only make sense if `1 = LED on, 0 = LED off`.

The vendor firmware's own blanking sequence (`P0 = 0; P2 = select;
P0 = data`) has the same implication: `P0 = 0` is called "blank" in the
README's measurement notes, which is only true under active-high.

If P0 is actually active-high, then `main.c`'s framebuffer helpers are
inverted: `fb_clear()` should set `0x00` (all off), and the set functions
should set bits rather than clear them. This cannot be resolved from source
— it requires watching the probe on a real cube and seeing whether
`(FE, 01)` lights one LED (active-high) or darkens one (active-low).

## Where this is going

The scan kernel above is the fixed part; the frames are the part a user should
be able to author. The intended shape:

1. a `ledcube` block surface — set/clear a voxel, fill a plane, shift the cube,
   hold a frame for *n* ms — over a 4x4x4 model;
2. `generateC()` emits `frames[N][8]` plus this same scan loop, so what the
   blocks describe is what the chip runs;
3. the circuit designer renders the cube from the same frame data, integrating
   brightness over ~20 ms the way an eye does, which is exactly what bw-board's
   LED model already does for a single LED.

Step 3 is why the refresh rate above matters: at 101 Hz a voxel is lit 1/8 of
the time, so a correctly simulated cube should look dimmer than a
statically-driven LED by that factor, and a simulator that shows it at full
brightness is lying about what you will see.
