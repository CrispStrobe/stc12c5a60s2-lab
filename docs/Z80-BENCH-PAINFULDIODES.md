# Z80 bench, PainfulDiodes design — transcription in progress

Source: PainfulDiodes/z80-breadboard-computer (MIT, cloned to
`../../stc-research/corpus/z80-breadboard-computer/`). KiCad sheets
are s-expression text; extraction is a parse, not a squint.

**Doctrine (owner, 2026-08-16): designs are ADDITIVE.** This bench
does not replace the existing MC6850-ACIA z80-bench — the catalog
carries multiple Z80 designs side by side (ACIA-serial minimal,
this UM245R-USB-FIFO build, later the RC2014-paradigm backplane and
the 4-IC MCU-assisted minimal). Different real answers to the same
question are the pedagogy.

## Extracted so far (label pass, sheet by sheet)

- **z80_clock_reset**: CPU sheet — full A0-A15 + D0-D7 + ~MREQ ~IORQ
  ~RD ~WR emitted (clock + reset conditioning live here).
- **z80_memory**: ROM (AT28C64B, 8K) + RAM (AS6C62256, 32K) on the
  buses; selects ~ROM_CE / ~RAM_CE arrive from glue. ROM uses A0-A12,
  RAM A0-A14.
- **z80_glue_logic**: the decode — A15 splits ROM(low)/RAM(high);
  ~IORQ + A0 (+~RD/~WR) produce the port strobes ~RD_PORT_0,
  ~RD_PORT_1, WR_PORT_1 (a "~RD OR ~IORQ" intermediate is labeled).
- **z80_UM245R**: USB FIFO on D0-D7, driven by the three port
  strobes — port 0 read = status?, port 1 read/write = data (exact
  role assignment pending the pin-level pass).
- **z80_breadboard / _hld**: top sheets tying modules together.

## Next pass (the remaining transcription work)

1. Symbol inventory: KiCad 7 property syntax (`(property "Reference"
   …)`) — regex fix pending; gives refs/values per sheet.
2. Pin-level netlist: geometric join (wire segments x pin positions)
   or `kicad-cli sch export netlist` if available — then the
   RETRO-CONSOLE-style tables, two-source-checked against the repo's
   README-DETAILED.
3. Bench authoring: 3 boards, faithful; UM245R needs an engine part
   (FTDI-FIFO semantics = a byte pipe with RXF/TXE handshake — the
   serial face maps naturally).
Raw label inventory: corpus/z80-breadboard-computer/pd-z80-inventory.json

## Pin-level pass (tools/kicad-netlist.py, top flat sheet)

85 nets extracted from z80_breadboard.kicad_sch. The machine reads
clean off the output: U1=Z80 (all 40 pins placed), U5/U6=ROM+RAM
sharing A0-A14/D0-D7 with ~CE from the decode, U2=glue inverters,
U7=74HC245 bus transceiver gating D0-D7 to U8=UM245R USB FIFO
(~RXF U8.23->U7.4-ish handshake, ~TXE U8.22), U9=oscillator can
(CLK -> U1.6), SW1+R2+C5=the reset RC, and the port strobe
~WR_PORT_1 reaching the FIFO write side. Full raw table:
corpus/z80-breadboard-computer/netlist-z80_breadboard.txt.

**Defect RESOLVED (same night):** the over-merge had two real causes,
neither rotation: (1) multi-unit symbols contributed every unit's
pins to every instance, and the 74LS04's pin children are body-style
0, which a style!=1 filter then dropped entirely; (2) the UPSTREAM
schematic reuses refdes — its U1 is both the Z80 and a 74LS32
package, its U5 both the RAM and more gates. The tool now keeps the
lowest body style per unit, places only an instance's own unit plus
commons, qualifies refdes collisions (U1@74LS32), and carries two
soundness validators (no coinciding pins, no pin in two nets). The
flat sheet extracts 67 nets, zero warnings, and the control logic
reads as the design intends: ~RD -> both memories' ~OE + port gate;
~MREQ and ~IORQ into the LS32 OR-decode. Chip identities corrected:
U5 = AS6C62256 (32K RAM), U6 = AT28C64B (8K ROM), U7 = 74LS244
buffer, U2 = 74LS04 inverters, plus the LS32s under shared refdes.
The netlist is authoritative end to end; bench authoring proceeds on
it.
