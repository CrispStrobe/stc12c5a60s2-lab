#!/usr/bin/env python3
"""
normalize.py — turn raw ISP capture logs into a normalized frame table.

The captures in `docs/isp-captures/` are whatever the capturing program
happened to print. The spec (`docs/STC-ISP-PROTOCOL.md`) and the future
replay tests for `tools/stcbsl` both need the same thing out of them: an
ordered list of "these bytes went that way, during that phase". This
script is the only place that knows how to get from one to the other.

    ./tools/isp-capture/normalize.py --selftest
    ./tools/isp-capture/normalize.py docs/isp-captures/stc89c52rc/*.log -o frames/
    ./tools/isp-capture/normalize.py some.log --stats

Output is JSONL, one record per line; the schema is in README.md next to
this file. Every record carries its source file and line number, so any
claim in the spec can be walked back to the byte run it came from.

Design notes:

- The line-format layer is pluggable (see FORMATS). Formats are tried in
  order and the first that matches a line wins; a file may legitimately
  mix them. Adding a new capture tool means adding one function here, not
  touching anything else.
- Framing knowledge (magic, length, checksum) lives in one small block
  below and is used only to *annotate* records. A record is emitted
  whether or not it validates — a malformed frame is evidence, and
  silently dropping it would be the worst thing this script could do.
- Phase labels are derived from the protocol's own command bytes, not
  from any host tool's progress messages, so the labels stay meaningful
  across capture tools.

SPDX-License-Identifier: MIT
"""

import argparse
import json
import os
import re
import sys

# --------------------------------------------------------------------------
# Framing — annotation only. See docs/STC-ISP-PROTOCOL.md §4 for the
# derivation of each of these from the captures. Nothing here is required
# for a record to be emitted.
# --------------------------------------------------------------------------

MAGIC = b"\x46\xb9"
TERMINATOR = 0x16
DIR_BYTE = {0x68: "mcu->host", 0x6a: "host->mcu"}

# Command byte -> phase name. Direction matters for 0x00 and 0x80, which
# are reused in both directions with different meanings.
PHASE_BY_CMD = {
    ("mcu->host", 0x00): "status",
    ("host->mcu", 0x8f): "baud_probe",
    ("host->mcu", 0x8e): "baud_commit",
    ("host->mcu", 0x80): "link_test",
    ("host->mcu", 0x84): "erase",
    ("host->mcu", 0x00): "write",
    ("host->mcu", 0x8d): "options",
    ("host->mcu", 0x82): "run",
}


def frame_check(b):
    """Return (is_frame, notes) for a byte run, per §4 of the spec."""
    notes = []
    if len(b) < 8 or b[:2] != MAGIC:
        return False, ["not-a-frame"]
    ok = True
    if b[-1] != TERMINATOR:
        notes.append(f"bad-terminator=0x{b[-1]:02x}")
        ok = False
    declared = (b[3] << 8) | b[4]
    if declared != len(b) - 2:
        notes.append(f"bad-length declared={declared} actual={len(b) - 2}")
        ok = False
    csum = sum(b[2:-2]) & 0xFF
    if csum != b[-2]:
        notes.append(f"bad-checksum computed=0x{csum:02x} got=0x{b[-2]:02x}")
        ok = False
    if b[2] not in DIR_BYTE:
        notes.append(f"unknown-dir-byte=0x{b[2]:02x}")
    if ok and not notes:
        notes.append("frame-ok")
    return True, notes


# --------------------------------------------------------------------------
# Line formats. Each takes a single already-split line and returns
# (direction, bytes, leading_text) or None. `direction` is one of the
# DIR_BYTE values or None when the format cannot tell.
# --------------------------------------------------------------------------

_HEXPAIR = r"(?:[0-9A-Fa-f]{2}[ \t,]*)+"


def _hexbytes(s):
    return bytes.fromhex(re.sub(r"[^0-9A-Fa-f]", "", s))


def fmt_stcgal_debug(line):
    """`<text> <- Packet data: 46 B9 …` / `-> Packet data: …`.

    The format of the STC89C52RC bench captures. The leading text is the
    capturing tool's own progress message and is preserved as a note but
    never used to decide anything.
    """
    m = re.search(r"(<-|->)\s*Packet data:\s*(" + _HEXPAIR + r")", line)
    if not m:
        return None
    direction = "mcu->host" if m.group(1) == "<-" else "host->mcu"
    return direction, _hexbytes(m.group(2)), line[: m.start()].strip()


def fmt_labeled_ts(line):
    """`[  4.530501 ] programmer: 0x7f 0x7f …` — timestamped sniffer logs.

    Labels are mapped conservatively: anything that is not clearly the
    target is treated as the host, because a sniffer names the *device*
    it heard, and getting this backwards silently would be poison.
    """
    m = re.match(
        r"^\s*\[?\s*([\d.]+)\s*\]?\s*([A-Za-z_][\w ]*?)\s*:\s*((?:0x[0-9A-Fa-f]{1,2}[ \t,]*)+)\s*$",
        line,
    )
    if not m:
        return None
    label = m.group(2).strip().lower()
    mcu_labels = ("mcu", "target", "chip", "device", "slave")
    direction = "mcu->host" if any(k in label for k in mcu_labels) else "host->mcu"
    data = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{1,2})", m.group(3)))
    return direction, data, f"ts={m.group(1)} label={m.group(2).strip()}"


def fmt_arrow_hex(line):
    """`-> 46 b9 …`, `<- …`, `TX: …`, `RX: …` — bare direction markers."""
    m = re.match(
        r"^\s*(->|<-|>>|<<|>|<|TX|RX|tx|rx)\s*[:\s]\s*(" + _HEXPAIR + r")\s*$", line
    )
    if not m:
        return None
    mark = m.group(1).lower()
    direction = "mcu->host" if mark in ("<-", "<<", "<", "rx") else "host->mcu"
    return direction, _hexbytes(m.group(2)), ""


def fmt_pty_tap(line):  # TODO
    """TODO: a raw pty/serial tap (e.g. `socat`, `interceptty`).

    Not implemented because no such capture exists yet. A tap typically
    emits direction on a separate line from the data, so implementing it
    means giving formats a little state — do that by making this a small
    class with the same call signature rather than by adding globals.
    """
    return None


def fmt_logic_analyzer(line):  # TODO
    """TODO: sigrok/PulseView UART decoder CSV export.

    Columns are usually (time, channel, value); the channel names the
    wire, which maps to direction. Needed the day we sniff the 0x7F pulse
    train, which no host-side tool can show us (spec §5.1).
    """
    return None


FORMATS = [
    ("stcgal_debug", fmt_stcgal_debug),
    ("labeled_ts", fmt_labeled_ts),
    ("arrow_hex", fmt_arrow_hex),
    ("pty_tap", fmt_pty_tap),
    ("logic_analyzer", fmt_logic_analyzer),
]

PHASE_DIRECTIVE = re.compile(r"^\s*(?:#|;|//)\s*phase\s*[:=]\s*(\S+)", re.I)


# --------------------------------------------------------------------------
# Normalization
# --------------------------------------------------------------------------


def normalize_text(text, source="-", only_format=None, initial_phase="sync"):
    """Yield records (dicts) for one capture file's contents."""
    formats = [f for f in FORMATS if only_format is None or f[0] == only_format]
    if only_format and not formats:
        raise SystemExit(f"unknown --format {only_format!r}")

    phase = initial_phase
    seq = 0
    for lineno, rawline in enumerate(text.splitlines(), 1):
        # Progress bars rewrite the line with \r; each fragment is its own
        # logical line and a fragment can carry a packet.
        for line in rawline.split("\r"):
            if not line.strip():
                continue
            forced = PHASE_DIRECTIVE.match(line)
            if forced:
                phase = forced.group(1)
                continue
            hit = None
            for name, fn in formats:
                got = fn(line)
                if got:
                    hit = (name, got)
                    break
            if hit is None:
                continue
            fmt_name, (direction, data, text_note) = hit
            if not data:
                continue

            is_frame, notes = frame_check(data)
            if is_frame:
                cmd = data[5]
                if data[2] in DIR_BYTE and DIR_BYTE[data[2]] != direction:
                    notes.append(
                        f"direction-marker-disagrees-with-byte 0x{data[2]:02x}"
                    )
                new_phase = PHASE_BY_CMD.get((direction, cmd))
                if new_phase:
                    phase = new_phase
                record_phase = phase
            else:
                cmd = None
                record_phase = phase
                notes.append("noise")

            note = "; ".join(notes)
            if text_note:
                note = f"{note}; text={text_note}"
            seq += 1
            rec = {
                "phase": record_phase,
                "dir": direction,
                "bytes_hex": data.hex(" "),
                "note": note,
                "seq": seq,
                "src": f"{os.path.basename(source)}:{lineno}",
                "fmt": fmt_name,
            }
            if cmd is not None:
                rec["cmd"] = f"0x{cmd:02x}"
            yield rec


def stats(records):
    from collections import Counter

    c = Counter()
    for r in records:
        kind = "frame" if "noise" not in r["note"] else "noise"
        c[(r["phase"], r["dir"], kind)] += 1
    return c


# --------------------------------------------------------------------------
# Selftest — synthetic log, no hardware and no capture files needed.
# --------------------------------------------------------------------------

SELFTEST_LOG = """\
Waiting for MCU, please cycle power: <- Packet data: 00
<- Packet data: 46 B9 68 00 08 00 0A 79 F3 16
Target frequency: 11.030 MHz
Switching to 115200 baud: checking -> Packet data: 46 B9 6A 00 0C 8F FF FD 00 06 A0 81 28 16
 <- Packet data: 46 B9 68 00 0C 8F FF FD 00 06 A0 81 26 16
Erasing 2 blocks: -> Packet data: 46 B9 6A 00 0D 84 02 33 33 33 33 33 33 2F 16
 <- Packet data: 46 B9 68 00 0D 80 F0 02 C4 2B 01 83 74 CE 16
 -> Packet data: 46 B9 6A 00 06 82 F2 16
# phase: aftermath
[ 4.530501 ] programmer: 0x7f 0x7f 0x7f
[ 5.995480 ] mcu: 0x46 0xb9 0x68 0x00 0x06 0x80 0xee 0x16
-> 46 B9 6A 00 06 82 F2 17
"""

SELFTEST_EXPECT = [
    ("sync", "mcu->host", "00", "noise"),
    ("status", "mcu->host", "46 b9 68 00 08 00 0a 79 f3 16", "frame-ok"),
    ("baud_probe", "host->mcu", "46 b9 6a 00 0c 8f ff fd 00 06 a0 81 28 16", "frame-ok"),
    ("baud_probe", "mcu->host", "46 b9 68 00 0c 8f ff fd 00 06 a0 81 26 16", "frame-ok"),
    ("erase", "host->mcu", "46 b9 6a 00 0d 84 02 33 33 33 33 33 33 2f 16", "frame-ok"),
    ("erase", "mcu->host", "46 b9 68 00 0d 80 f0 02 c4 2b 01 83 74 ce 16", "frame-ok"),
    ("run", "host->mcu", "46 b9 6a 00 06 82 f2 16", "frame-ok"),
    ("aftermath", "host->mcu", "7f 7f 7f", "noise"),
    ("aftermath", "mcu->host", "46 b9 68 00 06 80 ee 16", "frame-ok"),
    ("run", "host->mcu", "46 b9 6a 00 06 82 f2 17", "bad-terminator"),
]


def selftest():
    got = list(normalize_text(SELFTEST_LOG, source="selftest.log"))
    fails = []
    if len(got) != len(SELFTEST_EXPECT):
        fails.append(f"record count {len(got)} != {len(SELFTEST_EXPECT)}")
    for i, (rec, exp) in enumerate(zip(got, SELFTEST_EXPECT)):
        phase, direction, hexs, notefrag = exp
        if rec["phase"] != phase:
            fails.append(f"[{i}] phase {rec['phase']!r} != {phase!r}")
        if rec["dir"] != direction:
            fails.append(f"[{i}] dir {rec['dir']!r} != {direction!r}")
        if rec["bytes_hex"] != hexs:
            fails.append(f"[{i}] bytes {rec['bytes_hex']!r} != {hexs!r}")
        if notefrag not in rec["note"]:
            fails.append(f"[{i}] note {rec['note']!r} lacks {notefrag!r}")

    # The three formats must all have fired, or the fixture stopped
    # covering what it claims to cover.
    seen = {r["fmt"] for r in got}
    for want in ("stcgal_debug", "labeled_ts", "arrow_hex"):
        if want not in seen:
            fails.append(f"format {want} never matched")

    # Framing arithmetic, checked independently of the fixture.
    good = bytes.fromhex("46b96a000682f216")
    if frame_check(good) != (True, ["frame-ok"]):
        fails.append("frame_check rejected a known-good frame")
    bad = bytearray(good)
    bad[6] ^= 0xFF
    if "bad-checksum" not in " ".join(frame_check(bytes(bad))[1]):
        fails.append("frame_check missed a corrupted checksum")

    for f in fails:
        print("FAIL:", f)
    print(f"selftest: {len(got)} records, {len(fails)} failures")
    return 1 if fails else 0


# --------------------------------------------------------------------------


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("files", nargs="*", help="capture logs ('-' for stdin)")
    ap.add_argument(
        "-o",
        "--out",
        help="output file, or a directory to write one <name>.jsonl per input",
    )
    ap.add_argument("--format", help="force one line format (default: try all)")
    ap.add_argument("--phase", default="sync", help="phase label before the first frame")
    ap.add_argument("--stats", action="store_true", help="print a phase/direction summary")
    ap.add_argument(
        "--strict",
        action="store_true",
        help="exit non-zero if any emitted frame fails validation",
    )
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()
    if not args.files:
        ap.error("no input files (or use --selftest)")

    outdir = args.out if args.out and (args.out.endswith("/") or os.path.isdir(args.out)) else None
    if outdir:
        os.makedirs(outdir, exist_ok=True)
    single = None
    if args.out and not outdir:
        single = open(args.out, "w")

    bad = 0
    for path in args.files:
        text = sys.stdin.read() if path == "-" else open(path, errors="replace").read()
        records = list(
            normalize_text(text, source=path, only_format=args.format, initial_phase=args.phase)
        )
        bad += sum(1 for r in records if "bad-" in r["note"])
        if outdir:
            name = os.path.splitext(os.path.basename(path))[0] + ".jsonl"
            fh = open(os.path.join(outdir, name), "w")
        else:
            fh = single or sys.stdout
        for r in records:
            fh.write(json.dumps(r) + "\n")
        if outdir:
            fh.close()
        if args.stats:
            print(f"--- {path}: {len(records)} records", file=sys.stderr)
            for (phase, direction, kind), n in sorted(stats(records).items()):
                print(f"    {phase:12s} {direction:10s} {kind:5s} {n}", file=sys.stderr)
    if single:
        single.close()
    if args.strict and bad:
        print(f"{bad} record(s) failed frame validation", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
