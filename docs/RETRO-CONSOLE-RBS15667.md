# Retro console (RBS15667) — hardware spec, transcribed

Netlist transcribed 2026-08-16 from the vendor manual's schematic
(page 5, archived at `../../stc-research/corpus/roboter-bausatz/
RBS15667-retrokonsole.pdf`, extracted at 3x as `p5-schematic-3x-1.png`).
This file records **circuit topology and observed facts only** — no
vendor art, text, or firmware. The owner holds the physical kit; any
ambiguity below is settled by continuity test on the real board.

Target: 100% functional equivalence in the simulator, and the
re-programming story — the same program runs in-app and flashes onto
the real, socketed chip via `make PART=stc15f2k60s2 flash`.

## U1 — STC15F2K60S2, DIP-40, socketed

Pinout matches `PINOUT-STC15.md` (VCC pin 18, GND pin 20, RST/P5.4
pin 17, P0 ascending from pin 1). Decoupling: C1 10 µF electrolytic +
C2 100 nF at VCC. No crystal — internal RC (P1.6/P1.7 stay free of
XTAL duty; P1.6 is used as I/O here, see dis3).

## Power / misc

| Node | Wiring |
|---|---|
| J1 | USB 5 V in, power only — no data lines reach the MCU |
| S6 | power switch between J1/battery and VCC rail |
| Battery holder | alternative supply (3–5 V per manual) |
| LS1 buzzer | VCC → speaker → Q1 (S8550, PNP) emitter–collector → GND; base ← 1 k (R1) ← **P5.5**. PNP high-side: **drive P5.5 LOW to sound** |

## The shared scan bus

Eight lines appear on *every* display (both matrices and the 7-seg):

```
P1.5  P4.1  P2.4  P2.2  P2.1  P2.0  P4.4  P1.4
```

One multiplex loop serves the whole console; each display contributes
its own column/select group. (Which of bus/select is anode vs cathode
follows the 1088AS/056SMG polarity — verify on the physical board
before finalizing the driver; the simulator part should make polarity
a param.)

## dis2 — 8×8 LED matrix (upper half of the 16×8 playfield)

| Matrix pin | 16 | 15 | 14 | 13 | 12 | 11 | 10 | 9 |
|---|---|---|---|---|---|---|---|---|
| MCU | P1.5 | P4.1 | **P4.5** | P2.4 | **P2.7** | P2.2 | P2.1 | **P2.6** |

| Matrix pin | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| MCU | **P0.0** | **P0.1** | P2.0 | P4.4 | **P0.2** | P1.4 | **P0.3** | **P0.4** |

Unique to dis2 (bold): P4.5, P2.7, P2.6, P0.0–P0.4.

## dis3 — 8×8 LED matrix (lower half of the playfield)

| Matrix pin | 16 | 15 | 14 | 13 | 12 | 11 | 10 | 9 |
|---|---|---|---|---|---|---|---|---|
| MCU | P1.5 | P4.1 | **P0.7** | P2.4 | **P0.6** | P2.2 | P2.1 | **P0.5** |

| Matrix pin | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| MCU | **P1.0** | **P1.1** | P2.0 | P4.4 | **P1.6** | P1.4 | **P1.2** | **P1.3** |

Unique to dis3: P0.5–P0.7, P1.0–P1.3, P1.6. Note the shared pins sit
at the SAME matrix pin positions on both (16, 15, 13, 11, 10, 3, 4, 6)
— the two 1088AS are electrically parallel on the scan bus and differ
only in their column groups: a clean 16×8.

## dis1 — 056SMG_3, 3-digit 0.56" 7-seg (score), 12-pin

| Pin | 12 | 11 | 10 | 9 | 8 | 7 |
|---|---|---|---|---|---|---|
| MCU | **P2.3** | P4.1 | P2.2 | **P2.5** | **P4.2** | P2.4 |

| Pin | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|
| MCU | P4.4 | P2.0 | (n/c or P2.1 — verify on board) | P2.1 | P1.4 | (n/c — verify) |

Unique to dis1: P2.3, P2.5, P4.2 — consistent with three digit-select
lines for three digits, segments riding the shared bus. Pins 3/6
labels sit at the legibility edge in the scan; settle on the board.

## Buttons — five, all SPST to GND (internal pull-ups do the rest)

| Switch | MCU pin | Note |
|---|---|---|
| S1 | P3.0 | also STC ISP RxD — bootloader owns it at cold power-on, firmware after |
| S2 | P3.2 | |
| S3 | P3.3 | |
| S4 | P3.7 | |
| S5 | P3.6 | |

## Free pins

P3.1 (TxD — free, so serial debug prints are possible alongside S1),
P3.4, P3.5, P1.7, P5.4 (RST).

## Simulator plan

1. bw-parts: generic NxM matrix (16×8 = two 1088AS on a shared bus) +
   3-digit 7-seg part (in flight, briefed 2026-08-16).
2. Bench example seating U1 + dis1–dis3 + Q1/R1/LS1 + five buttons,
   wired exactly per the tables above.
3. Games in pseudocode (`DEVICE STC15F2K60S2`), clean-room.
4. Flash the real kit — the first verified-on-silicon milestone.
