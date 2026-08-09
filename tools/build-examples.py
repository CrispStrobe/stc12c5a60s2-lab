#!/usr/bin/env python3
"""
build-examples.py — one pseudocode program, all the way through, as a bundle.

Every layer of this project has been tested alone: the emitter has its
round-trip tests, the emulators have differential execution, the board layer
has a scripted MCU, the monitor has its command-layer tests. Nothing has
tested whether they COMPOSE, because nothing produced an artifact all of them
consume.

This does. For each `pseudocode/*.bw` it writes an `examples/<name>/` bundle:

    <name>.bw           the source, copied so the bundle stands alone
    <name>.c            what the emitter produced
    <name>.hex          what SDCC produced, ready for stcgal
    <name>.cdb          SDCC debug info, for the symbol table
    pins.json           boundary C input: {device, clock, pins}
    symbols.json        boundary D input, multi-WHEN programs only
    meta.json           sizes, task count, which inferNetlist rows it covers

Consumed by, in order of how much it matters:

  * the circuit designer   pins.json -> inferNetlist -> a board to draw
  * both emulators         .hex, plus symbols.json for Level 1 position
  * the debug monitor      symbols.json over its SYMS command
  * anyone flashing a chip .hex

Usage:  make examples        (or: python3 tools/build-examples.py)

Needs SDCC and a checkout of stc-compiler beside this repo. It is a developer
tool, not part of the firmware build.
"""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
COMPILER = REPO.parent / "stc-compiler"

if not (COMPILER / "stc_pseudocode.py").exists():
    sys.exit(f"build-examples: need stc-compiler beside this repo, looked in {COMPILER}")
sys.path.insert(0, str(COMPILER))

import stc_pseudocode                                        # noqa: E402
import stc_symtab                                            # noqa: E402

# Per-part SDCC limits, the same table the Makefile applies.
TARGETS = {
    "stc12c5a60s2": ["--iram-size", "256", "--xram-size", "1024", "--code-size", "61440"],
    "stc15f2k60s2": ["--iram-size", "256", "--xram-size", "1792", "--code-size", "61440"],
    "stc89c52rc":   ["--iram-size", "256", "--xram-size", "256",  "--code-size", "8192"],
}

# The rows inferNetlist must be able to suggest. The first four are the table
# in simulation-contract.md boundary C; TONE is a fifth that the contract does
# not yet have, and 07-buzzer is what exposed it -- a buzzer is not an LED with
# a resistor, and suggesting one would be wrong rather than merely unhelpful.
NETLIST_ROWS = {
    "analog":     "ANALOG -> potentiometer across VCC/GND, wiper to pin",
    "input":      "INPUT -> button to GND plus a pull-up",
    "output-low": "OUTPUT ACTIVE LOW -> VCC, 1k, LED, pin",
    "output":     "OUTPUT -> pin, 1k, LED, GND",
    "tone":       "TONE -> buzzer between pin and GND  (NOT in the contract yet)",
    "port":       "PORT OUTPUT -> eight loads on one port, e.g. a 7-segment digit"
                  "  (NOT in the contract yet)",
    "part-595":   "PART 74HC595 -> shift register, 3 pins in / 8 loads out"
                  "  (NOT in the contract yet)",
}


def netlist_row(pin) -> str:
    if pin.direction in ("analog", "input", "tone"):
        return NETLIST_ROWS[pin.direction]
    # A PWM LED is wired exactly like a plain one; only the drive differs.
    return NETLIST_ROWS["output-low" if pin.active_low else "output"]


def find_sdcc() -> str:
    for candidate in ("sdcc", str(COMPILER / "bin" / "sdcc")):
        try:
            subprocess.run([candidate, "--version"], check=True, capture_output=True)
            return candidate
        except (OSError, subprocess.CalledProcessError):
            continue
    sys.exit("build-examples: no runnable sdcc (brew install sdcc)")


def build_one(bw_path: Path, sdcc: str, outroot: Path) -> dict:
    name = bw_path.stem
    out = outroot / name
    out.mkdir(parents=True, exist_ok=True)

    source = bw_path.read_text()
    program = stc_pseudocode.parse(source)
    c_text = stc_pseudocode.emit_c(program)

    shutil.copyfile(bw_path, out / bw_path.name)
    (out / f"{name}.c").write_text(c_text)

    part = program.part.lower()
    if part not in TARGETS:
        sys.exit(f"{name}: unknown DEVICE {program.part}")

    # --debug so the .cdb carries the addresses the symbol table needs.
    subprocess.run(
        [sdcc, "-mmcs51", "--std-c99", "--debug", *TARGETS[part],
         f"-DFOSC_HZ={program.clock}UL", "-o", str(out) + "/", str(out / f"{name}.c")],
        check=True, capture_output=True,
    )
    ihx = out / f"{name}.ihx"
    hex_path = out / f"{name}.hex"
    hex_path.write_text(
        subprocess.run(["packihx", str(ihx)], check=True, capture_output=True, text=True).stdout
    )

    # ---- boundary C input -------------------------------------------------
    pins = [{
        "name": p.name,
        "port": p.port,
        "bit": p.bit,
        "pin": f"P{p.port}.{p.bit}",
        "direction": p.direction,
        "activeLow": p.active_low,
    } for p in program.pins.values()]

    # Whole ports are part of boundary C's input too, and were not: an example
    # that declares only a PORT produced an empty pins.json, so the circuit
    # designer had nothing to draw for a display.
    # Parts are boundary C input too: a PART is three pins in and eight loads
    # out, and the designer cannot draw the far side without being told.
    parts = [{
        "name": w.name,
        "kind": w.kind,
        "pins": {"data": f"P{w.data[0]}.{w.data[1]}",
                 "clock": f"P{w.clock[0]}.{w.clock[1]}",
                 "latch": f"P{w.latch[0]}.{w.latch[1]}"},
        "outputs": 8,
        "activeLow": w.active_low,
    } for w in program.parts.values()]

    ports = [{
        "name": w.name,
        "port": w.port,
        "sfr": f"P{w.port}",
        "width": 8,
        "direction": w.direction,
        "activeLow": w.active_low,
    } for w in program.ports.values()]

    (out / "pins.json").write_text(json.dumps(
        {"device": program.part, "clock": program.clock,
         "pins": pins, "ports": ports, "parts": parts}, indent=2) + "\n")

    # ---- boundary D input, when there is a scheduler ----------------------
    symbols = None
    if len(program.whens) > 1:
        symbols = stc_symtab.build_symbol_table(
            (out / f"{name}.cdb").read_text(), c_text,
            fosc=program.clock, device=part,
        )
        (out / "symbols.json").write_text(json.dumps(symbols, indent=2) + "\n")

    # SDCC leaves a dozen intermediates behind. A bundle is meant to be read
    # and committed, so keep only what a consumer actually uses.
    KEEP = {".bw", ".c", ".hex", ".cdb", ".json"}
    for leftover in out.iterdir():
        if leftover.is_file() and leftover.suffix not in KEEP:
            leftover.unlink()

    meta = {
        "name": name,
        "device": program.part,
        "clock": program.clock,
        "tasks": len(program.whens),
        "scheduler": len(program.whens) > 1,
        "hexBytes": len(hex_path.read_bytes()),
        "netlistRows": sorted(
            {netlist_row(p) for p in program.pins.values()}
            | {NETLIST_ROWS["port"] for w in program.ports.values()
               if w.direction == "output"}
            | {NETLIST_ROWS["part-595"] for _ in program.parts.values()}),
        "artifacts": sorted(p.name for p in out.iterdir() if p.is_file()),
    }
    (out / "meta.json").write_text(json.dumps(meta, indent=2) + "\n")
    return meta


def main() -> int:
    sdcc = find_sdcc()
    outroot = REPO / "examples"
    outroot.mkdir(exist_ok=True)

    metas = []
    for bw in sorted((REPO / "pseudocode").glob("*.bw")):
        meta = build_one(bw, sdcc, outroot)
        metas.append(meta)
        tag = f"{meta['tasks']} task" + ("s" if meta["tasks"] > 1 else "")
        print(f"  {meta['name']:20} {meta['hexBytes']:>6} B hex   {tag}")

    covered = sorted({row for m in metas for row in m["netlistRows"]})
    (outroot / "manifest.json").write_text(json.dumps(
        {"examples": metas, "netlistRowsCovered": covered}, indent=2) + "\n")

    print(f"\nnetlist rows covered by the set ({len(covered)}/{len(NETLIST_ROWS)}):")
    for row in covered:
        print(f"  {row}")
    missing = set(NETLIST_ROWS.values()) - set(covered)
    if missing:
        print("\n  WARNING: a preset with no example behind it is a preset nobody has run:")
        for row in sorted(missing):
            print(f"    {row}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
