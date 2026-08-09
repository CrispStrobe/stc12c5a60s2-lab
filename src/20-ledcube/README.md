# 4x4x4 bi-colour LED cube — measured, not assumed

The kit is the ICStation 4682 / HackerBox 005 cube: an STC12C5A60S2 driving 64
LEDs. This directory is the start of turning it into something you can author
animations for from blocks, and it begins where it has to — with what the
hardware actually does, measured under the simulator rather than read off a
forum post.

## Provenance

[`rgm3/ledcube444`](https://github.com/rgm3/ledcube444) is **MIT** (rgm, 2016):
the vendor's Keil sources reworked to build on SDCC. That licence is why this
topic can come back into the repo at all — the vendor's own `4681.zip` package
is unlicensed and stays out (see the note in `CLAUDE.md`). Nothing here is
derived from that package; `probe.c` is ours, written against the scan model
measured below.

## What the hardware does

Run under `emu8051-stc` at FOSC = 11.0592 MHz, the vendor firmware shows its
hand immediately:

| | |
|---|---|
| select lines | `P2`, **active low**, cycling `FE FD FB F7 EF DF BF 7F` |
| data | `P0`, 8 bits, written *after* the select |
| step | **1.235 ms** |
| frame | 8 steps = **9.88 ms**, so the cube refreshes at **101 Hz** |
| frame format | 8 bytes: one `P0` byte per select |

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
