# Z80 bench, PainfulDiodes design — netlist transcription

Source: PainfulDiodes/z80-breadboard-computer (MIT, cloned to
`../../stc-research/corpus/z80-breadboard-computer/`). KiCad sheets
are s-expression text; extraction is a parse, not a squint.

**Doctrine (owner, 2026-08-16): designs are ADDITIVE.** This bench
does not replace the existing MC6850-ACIA z80-bench — the catalog
carries multiple Z80 designs side by side (ACIA-serial minimal,
this UM245R-USB-FIFO build, later the RC2014-paradigm backplane and
the 4-IC MCU-assisted minimal). Different real answers to the same
question are the pedagogy.

## Provenance and the two-source rule

Everything below is reconciled across **two independent sources**:

1. **The KiCad schematics** — parsed geometrically by
   `tools/kicad-netlist.py` (union-find over wire segments and
   coincident pin/label points; per-unit library pin geometry;
   refdes-collision qualification). Five sheets:
   `z80_breadboard` (flat, complete), `z80_clock_reset`,
   `z80_memory`, `z80_glue_logic`, `z80_UM245R`.
2. **`README-DETAILED.md`** in the same repo — prose, truth tables
   and stated design rules.

Where they disagree, the disagreement is **flagged, not resolved by
preference** (see §9).

**Extraction status: SOUND (2026-08-16).** The earlier over-merge is
fixed, and the cause was not what this file first recorded. It was two
bugs in the extractor, both now closed:

- *Multi-unit symbols.* A KiCad hex inverter is one `lib_symbols`
  entry with one child symbol **per gate** (`74LS04_1_0`,
  `74LS04_2_0`, …). Collecting every child's pins for every instance
  put all six gates' pins on each gate and merged their nets. Fixed by
  honouring the instance's `(unit N)`.
- *Refdes collision in the upstream flat sheet.* `z80_breadboard`
  uses **`U1` for both the Z80 and a 74LS32 package**, and **`U5` for
  both the AS6C62256 and a second 74LS32**. That is a defect in the
  source drawing, not in the parser. The extractor now qualifies a
  colliding refdes with its library base (`U1@74LS32`, `U5@74LS32`)
  so the netlist tells the truth instead of shorting the CPU to a gate
  package. **This file uses `U1G`/`U5G` for those two 74LS32
  packages** to keep the tables readable.
- A third, smaller correction: the DeMorgan body-style filter dropped
  the 74LS04's gates entirely (its pin children are style `0`, not
  `1`).

The flat sheet and the four module sheets now agree pin-for-pin, and
the run reports **no pin in more than one net** (67 nets on the flat
sheet). There is no longer a "the module sheets are the control-signal
authority" caveat — both extract cleanly and both were cross-checked.

## 1. Bill of materials

| Ref (this doc) | Upstream ref | Part | Package | Role |
|---|---|---|---|---|
| U1 | `U1` (flat), `IC1` (clock sheet) | Zilog Z80 CPU | DIP-40 | the processor |
| U1G | `U1`@74LS32 (flat), `U1` (glue sheet) | 74LS32 quad 2-input OR | DIP-14 | 4 gates used |
| U5G | `U5`@74LS32 (flat), `U5` (glue sheet) | 74LS32 quad 2-input OR | DIP-14 | 2 used, 2 spare |
| U2 | `U2` | 74LS04 hex inverter | DIP-14 | 3 gates used |
| U5 | `U5` | AS6C62256-55PCN, 32K×8 SRAM | DIP-28 | RAM |
| U6 | `U6` | AT28C64B-15PU, 8K×8 EEPROM | DIP-28 | ROM |
| U7 | `U7` | 74LS244 octal tri-state buffer | DIP-20 | status port |
| U8 | `U8` | FTDI UM245R USB-parallel-FIFO module | 24-pin DIP module | host link |
| U9 | `U9` (flat), `IC9` (clock sheet) | ECS-2200B-100, 10 MHz oscillator can | DIP-8 footprint | clock |
| R1 | `R1` | 10 kΩ | | /RESET pull-up |
| R2 | `R2` | 220 Ω | | reset discharge limit |
| C3, C5 | `C3`, `C5` | 100 nF | | CPU decoupling, reset RC |
| C1, C2, C4 | glue sheet only | 100 nF | | 74xx decoupling |
| SW1 | `SW1` | SPST push | | reset button |
| IC2 | glue + clock sheets only | 74LS04 | DIP-14 | **§9.3 — probably not a real second package** |

The README also records the physical build practice: **9 breadboards**,
power fed to each board directly (never daisy-chained), a 22 µF
electrolytic across each board's rails, and a 100 nF ceramic at every
IC's power pins. The middle board is a component-free **backplane**
carrying the address and data buses on separate rows — an idea the
author explicitly reports as, in hindsight, probably not worth the 24
extra wires it cost.

## 2. U1 — Z80 CPU, DIP-40, every pin

| Pin | Name | Net | Goes to |
|---|---|---|---|
| 1 | A11 | A11 | U5.23, U6.23 |
| 2 | A12 | A12 | U5.2, U6.2 |
| 3 | A13 | A13 | U5.26 (RAM only) |
| 4 | A14 | A14 | U5.1 (RAM only) |
| 5 | A15 | A15 | U1G.13, U2.13 |
| 6 | /CLK | CLK | U9.5 (OUT) |
| 7 | D4 | D4 | U5.16, U6.16, U7.9, U8.2 |
| 8 | D3 | D3 | U5.15, U6.15, U7.12, U8.10 |
| 9 | D5 | D5 | U5.17, U6.17, U7.7, U8.8 |
| 10 | D6 | D6 | U5.18, U6.18, U7.5, U8.9 |
| 11 | VCC | +5V | R1.1, C3.1 |
| 12 | D2 | D2 | U5.13, U6.13, U7.14, U8.3 |
| 13 | D7 | D7 | U5.19, U6.19, U7.3, U8.6 |
| 14 | D0 | D0 | U5.11, U6.11, U7.18, U8.1 |
| 15 | D1 | D1 | U5.12, U6.12, U7.16, U8.5 |
| 16 | /INT | +5V | tied inactive |
| 17 | /NMI | +5V | tied inactive |
| 18 | /HALT | — | **deliberately unconnected** |
| 19 | /MREQ | /MREQ | U1G.10, U1G.12 |
| 20 | /IORQ | /IORQ | U1G.1, U5G.2 |
| 21 | /RD | /RD | U5.22 (/OE), U6.22 (/OE), U5G.1 |
| 22 | /WR | /WR | U5.27 (/WE), U1G.2 |
| 23 | /BUSACK | — | **deliberately unconnected** |
| 24 | /WAIT | +5V | tied inactive |
| 25 | /BUSRQ | +5V | tied inactive |
| 26 | /RESET | /RESET | R1.2, R2.1, C5.1 |
| 27 | /M1 | — | **deliberately unconnected** |
| 28 | /RFSH | — | **deliberately unconnected** |
| 29 | GND | GND | |
| 30 | A0 | A0 | U5.10, U6.10, U2.11, U5G.4 |
| 31 | A1 | A1 | U5.9, U6.9 |
| 32 | A2 | A2 | U5.8, U6.8 |
| 33 | A3 | A3 | U5.7, U6.7 |
| 34 | A4 | A4 | U5.6, U6.6 |
| 35 | A5 | A5 | U5.5, U6.5 |
| 36 | A6 | A6 | U5.4, U6.4 |
| 37 | A7 | A7 | U5.3, U6.3 |
| 38 | A8 | A8 | U5.25, U6.25 |
| 39 | A9 | A9 | U5.24, U6.24 |
| 40 | A10 | A10 | U5.21, U6.21 |

A0–A12 and D0–D7 are **fully shared** between RAM and ROM: the two
28-pin devices have a compatible pinout, which is the whole reason the
build wires them in parallel. A13/A14 reach the RAM only.

## 3. Clock and reset

| Ref | Pin | Net |
|---|---|---|
| U9 | 1 (Tri-State / OE) | **unconnected** — internal pull-up leaves the can enabled |
| U9 | 4 (GND) | GND |
| U9 | 5 (OUT) | → U1.6 (/CLK) |
| U9 | 8 (VDD) | +5V |
| R1 (10 k) | 1 / 2 | +5V / /RESET |
| C5 (100 nF) | 1 / 2 | /RESET / GND |
| R2 (220 Ω) | 1 / 2 | /RESET / SW1.2 |
| SW1 | 1 / 2 | GND / R2.2 |
| C3 (100 nF) | 1 / 2 | +5V / GND (CPU decoupling) |

Power-on: C5 starts discharged, so /RESET is low (active) and rises
through R1·C5 ≈ 1 ms. Press SW1 and C5 discharges to GND through the
220 Ω R2; release and it recharges the same way, which also debounces
the switch. The Z80's /RESET is **active low** — the opposite of the
STC12 convention documented elsewhere in this repo, and a trap when
moving between the two benches.

## 4. Memory — U5 (RAM) and U6 (ROM)

Both DIP-28 and pin-compatible over the shared range.

| Pin | AS6C62256 (U5, RAM) | net | AT28C64B (U6, ROM) | net |
|---|---|---|---|---|
| 1 | A14 | A14 | NC | **unconnected** |
| 2 | A12 | A12 | A12 | A12 |
| 3–10 | A7 A6 A5 A4 A3 A2 A1 A0 | A7…A0 | A7 A6 A5 A4 A3 A2 A1 A0 | A7…A0 |
| 11–13 | D0 D1 D2 | D0–D2 | D0 D1 D2 | D0–D2 |
| 14 | VSS | GND | GND | GND |
| 15–19 | D3 D4 D5 D6 D7 | D3–D7 | D3 D4 D5 D6 D7 | D3–D7 |
| 20 | /CE | **/RAM_CE** ← U1G.8 | /CE | **/ROM_CE** ← U1G.11 |
| 21 | A10 | A10 | A10 | A10 |
| 22 | /OE | /RD | /OE | /RD |
| 23 | A11 | A11 | A11 | A11 |
| 24 | A9 | A9 | A9 | A9 |
| 25 | A8 | A8 | A8 | A8 |
| 26 | A13 | A13 | NC | **unconnected** |
| 27 | /WE | /WR | /WE | **+5V — write disabled** |
| 28 | VCC | +5V | VCC | +5V |

Both /OE pins hang on the CPU's /RD, so a device drives the bus only
during a read. That is also why the Z80's refresh cycles are harmless:
refresh asserts /MREQ (so a /CE does go active) but neither /RD nor
/WR, so nothing drives the bus and nothing is written.

## 5. Glue logic — gate by gate

Three packages, nine gates used. The extraction resolves **which unit
of which package** carries each function; the README supplies the
intent and the truth tables. They agree completely.

### U1G — 74LS32 quad OR (all four gates used)

| Gate | A (pin) | B (pin) | Y (pin) | Function |
|---|---|---|---|---|
| 1 | /IORQ (1) | /WR (2) | /WR_PORT_1 (3) | `/WR_PORT_1 = /IORQ OR /WR` |
| 2 | /A0 (4) ← U2.10 | /RD OR /IORQ (5) | /RD_PORT_1 (6) | `/RD_PORT_1 = /A0 OR (/RD OR /IORQ)` |
| 3 | /A15 (9) ← U2.12 | /MREQ (10) | /RAM_CE (8) | `/RAM_CE = /A15 OR /MREQ` |
| 4 | /MREQ (12) | A15 (13) | /ROM_CE (11) | `/ROM_CE = /MREQ OR A15` |

### U5G — 74LS32 quad OR (two used, two spare)

| Gate | A (pin) | B (pin) | Y (pin) | Function |
|---|---|---|---|---|
| 1 | /RD (1) | /IORQ (2) | /RD OR /IORQ (3) | the shared I/O-read term |
| 2 | A0 (4) | /RD OR /IORQ (5) | /RD_PORT_0 (6) | `/RD_PORT_0 = A0 OR (/RD OR /IORQ)` |
| 3 | GND (9) | GND (10) | n/c (8) | spare, inputs tied low |
| 4 | GND (12) | GND (13) | n/c (11) | spare, inputs tied low |

### U2 — 74LS04 hex inverter (three used)

| Gate | A (pin) | Y (pin) | Function |
|---|---|---|---|
| 4 | /WR_PORT_1 (9) ← U1G.3 | WR_PORT_1 (8) → U8.18 | the UM245R wants write **active high** |
| 5 | A0 (11) | /A0 (10) → U1G.4 | |
| 6 | A15 (13) | /A15 (12) → U1G.9 | |
| 1–3 | not instantiated anywhere | | |

**The idiom, stated once:** with active-low signals an OR gate *is* an
AND. `/ROM_CE = /MREQ OR A15` reads as "ROM is selected when the CPU
requests memory **and** A15 is low". Every rule above is that trick,
and it is why the whole decode fits in one and a half OR packages.

## 6. Memory map and port map

```
 FFFF ┐
      │  RAM   AS6C62256, 32K, A0-A14 all connected — linear
 8000 ┘
 7FFF ┐
      │  ROM   AT28C64B, 8K, but /ROM_CE covers the whole low 32K
 0000 ┘        and A13/A14 are NOT connected, so the 8K image
               ALIASES four times: 0000, 2000, 4000, 6000
```

The aliasing is the author's own stated observation, and he calls it
harmless: reset vectors to 0x0000, which is ROM, which is what starts
the machine.

Ports are decoded on **A0 alone**, so every even port is port 0 and
every odd port is port 1:

| Access | Condition | Selects |
|---|---|---|
| `IN A,(even)` | /IORQ · /RD · /A0 | 74LS244 status byte |
| `IN A,(odd)` | /IORQ · /RD · A0 | UM245R read (a received byte) |
| `OUT (any),A` | /IORQ · /WR | UM245R write — **A0 is not decoded on writes at all** |

Dropping A0 from the write decode is a deliberate simplification: the
machine only ever writes to one port.

Interrupt-acknowledge is not a hazard even though /M1 goes undecoded:
an INTA cycle asserts /IORQ with /M1 while /RD and /WR both stay
inactive, so no port strobe fires — and /INT is tied high anyway.

## 7. U7 — 74LS244 status port

| Pin | Name | Net |
|---|---|---|
| 1 | /OEa | **/RD_PORT_0** ← U5G.6 |
| 19 | /OEb | **/RD_PORT_0** ← U5G.6 |
| 2 | I0a | ← U8.22 (**/TXE**) |
| 18 | O0a | → **D0** |
| 4 | I1a | ← U8.23 (**/RXF**) |
| 16 | O1a | → **D1** |
| 6, 8 | I2a, I3a | GND |
| 14, 12 | O2a, O3a | → D2, D3 |
| 11, 13, 15, 17 | I0b–I3b | GND |
| 9, 7, 5, 3 | O0b–O3b | → D4, D5, D6, D7 |
| 10 | GND | GND |
| 20 | VCC | +5V |

Both halves are enabled by the same strobe, so a read of port 0
returns a whole byte: two live status bits and six hard zeros. **The
bit assignment is where the two sources conflict — see §9.1.**

## 8. U8 — UM245R USB FIFO module

| Pin | Name | Net |
|---|---|---|
| 1 | D0 | D0 |
| 2 | D4 | D4 |
| 3 | D2 | D2 |
| 5 | D1 | D1 |
| 6 | D7 | D7 |
| 7 | GND | GND |
| 8 | D5 | D5 |
| 9 | D6 | D6 |
| 10 | D3 | D3 |
| 12 | /RD | **/RD_PORT_1** ← U1G.6 |
| 15 | VCC | +5V |
| 18 | WR | **WR_PORT_1** ← U2.8 (active **high**) |
| 20 | /RESET | **unconnected** |
| 21 | VCC | +5V |
| 22 | /TXE | → U7.2 |
| 23 | /RXF | → U7.4 |
| 24 | GND | GND |

Pins 4, 11, 13, 14, 16, 17 and 19 are **absent from the upstream KiCad
symbol** — it models only what the design uses. They are left unrecorded
here rather than guessed from an FTDI datasheet.

Behavioural facts the README establishes by experiment, which any
model of this part must honour:

- With an empty receive FIFO the UM245R **repeats the last byte
  received**; it does not return 0. So software must test /RXF first —
  a read is never self-validating.
- /RXF low = a byte is waiting. /TXE low = there is room to transmit.
- Its write strobe is active **high**, uniquely among this machine's
  control signals; hence the inverter, U2 gate 4.
- The FIFO exists precisely to dodge the timing/data-loss problem the
  author hit with FTDI-cable + 6850-ACIA builds. That is the design's
  reason for being, and the exact contrast with the catalog's ACIA
  bench.

### Engine part spec — what a `um245r` device would need (deferred)

Filed here for the parts lane; **no such part exists in the engine
today**, which is why the bench example ships the serial face missing
rather than faked.

- Pins: `D0–D7` (bidirectional, tri-state), `/RD` (in), `WR` (in),
  `/TXE` (out), `/RXF` (out), `/RESET` (in), VCC/GND.
- Two byte FIFOs (host→device "receive", device→host "transmit");
  128 rx / 384 tx on the real FT245BM behind the module.
- `/RD` low → drive D0–D7 with the head of the receive FIFO; pop on
  the rising edge. **An empty FIFO drives the last byte again** — model
  it, do not smooth it away, or the classic student bug (reading
  without checking /RXF) stops reproducing, and reproducing it is the
  pedagogical point.
- `WR` high→low edge latches D0–D7 into the transmit FIFO.
- `/RXF` low while the receive FIFO is non-empty; `/TXE` low while the
  transmit FIFO has room.
- Host face: the same byte pipe the app already gives a serial
  terminal — the mapping is natural, which is why this is worth
  building rather than approximating with an ACIA.

## 9. Ambiguities — flagged, not guessed

### 9.1 Status-bit order: schematic and README disagree

**README-DETAILED.md** (§USB) says: *"/RXF as bit 0 and /TXE as bit
1"*.

**The schematic** — both `z80_UM245R` and the flat `z80_breadboard`,
which agree with each other — wires:

```
U8.22 (/TXE) -> U7.2 (I0a) -> U7.18 (O0a) -> D0
U8.23 (/RXF) -> U7.4 (I1a) -> U7.16 (O1a) -> D1
```

i.e. **bit 0 = /TXE, bit 1 = /RXF** — the reverse of the prose.

Not resolvable from the corpus. The tie-breaker lives in the companion
monitor's `UM245R.asm` (github.com/PainfulDiodes/marvin), which the
README itself names as the file that "ties Marvin to the circuit
design"; that repo is not in the local corpus. **Anything implementing
this port must state which convention it chose.** Two independent
drawings beat one paragraph, so the schematic reading is the better
bet — but it is a bet, and it is recorded as one.

### 9.2 The 74xx power and decoupling pins

The flat `z80_breadboard` sheet connects no VCC/GND on U1G, U5G or U2;
the glue sheet does, together with C1/C2/C4. Ordinary schematic
practice (power units drawn once, in a corner), not a missing wire —
recorded because a naive netlist diff between the two sheets shows it
as a difference.

### 9.3 IC2 — a second 74LS04 that probably is not real

The glue and clock/reset sheets each instantiate `IC2`, a 74LS04, at
units 3/4/5, inputs tied to GND, outputs dangling, in the same corner
block as the decoupling capacitors. It appears in **no** functional net
and is **absent from the flat schematic**. Reading: a spare-gate /
unused-input annotation block copy-pasted between sheets, carrying the
wrong refdes — U2's genuinely unused gates are units 1–3, not 3–5.
Treated here as **not part of the machine**. Only the physical board
settles it.

### 9.4 Reset RC value

The README describes the reset capacitor as 0.1 µF against a 10 k
pull-up, giving ~1 ms — short by the standards of most 8-bit power-on
reset circuits, though far longer than the Z80's 3-clock minimum. The
schematic's C5 value field agrees (100 nF). No conflict; recorded
because it is the sort of number a reader will want to check.

### 9.5 "74LS42"

The README's glue-logic section writes *"a 74LS42 contains 4 OR
gates"*. The 74LS42 is a BCD-to-decimal decoder; the design uses the
**74LS32**, which is what every schematic instance says. A typo in
prose, not a design question.

## 10. What was built from this

`examples/z80-pd-bench/` in sb3-creator — the bench example, seated on
three breadboards. What it models faithfully and what it approximates
is stated in that directory's `EXPECTED.md`, not here.
