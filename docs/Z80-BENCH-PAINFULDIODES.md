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
