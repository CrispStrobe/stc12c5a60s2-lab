#!/usr/bin/env python3
"""
live-monitor.py — the host end of boundary D.

Talks the protocol in include/live-proto.h to firmware built from
src/10-live-firmware. It is deliberately a *reference* implementation rather
than a debugger UI: the point is that the wire format is written down clearly
enough for two independent implementations to agree, so this one is separate
from the C one on purpose and is diffed against it by `make test`.

    ./tools/live-monitor.py --selftest              # no hardware needed
    ./tools/live-monitor.py --port /dev/cu.usbserial-XXXX
    ./tools/live-monitor.py --port ... --watch      # live position display

What you get on real silicon is exactly the on-chip row of the capability
matrix in docs/DEBUG-CONTROL-MODEL.md §1: halt and step at yield points,
yield breakpoints, memory and register inspection, and an honest skew number
saying how much of the world went past while the program stood still. No
instruction stepping, no code breakpoints, no watchpoints. That is not this
tool being lazy; it is what the silicon allows.

SPDX-License-Identifier: MIT
"""

import argparse
import re
import subprocess
import sys
import time
from pathlib import Path

# --------------------------------------------------------------------------
# The wire format, parsed out of the C header rather than duplicated here.
# A constant that drifts between the two ends is exactly the failure this
# whole document set exists to prevent, so there is only one copy of it.
# --------------------------------------------------------------------------

PROTO_H = Path(__file__).resolve().parent.parent / "include" / "live-proto.h"


def _load_constants():
    text = PROTO_H.read_text()
    out = {}
    for name, value in re.findall(
        r"^#define\s+(LIVE_[A-Z0-9_]+)\s+(0x[0-9A-Fa-f]+|\d+)\s*(?:/\*.*)?$",
        text,
        re.M,
    ):
        out[name] = int(value, 0)
    return out


C = _load_constants()
if "LIVE_SOF" not in C:
    sys.exit(f"could not parse constants from {PROTO_H}")

SOF = C["LIVE_SOF"]
MAX_PAYLOAD = C["LIVE_MAX_PAYLOAD"]

SPACES = {
    "code": C["LIVE_SP_CODE"],
    "iram": C["LIVE_SP_IRAM"],
    "sfr": C["LIVE_SP_SFR"],
    "xram": C["LIVE_SP_XRAM"],
    "bit": C["LIVE_SP_BIT"],
}
SPACE_NAMES = {v: k for k, v in SPACES.items()}

STEP_NAMES = {
    C["LIVE_STEP_INSN"]: "insn",
    C["LIVE_STEP_LINE"]: "line",
    C["LIVE_STEP_BLOCK"]: "block",
    C["LIVE_STEP_OVER"]: "over",
    C["LIVE_STEP_OUT"]: "out",
}
BP_NAMES = {
    C["LIVE_BP_CODE"]: "code",
    C["LIVE_BP_YIELD"]: "yield",
    C["LIVE_BP_WRITE"]: "write",
    C["LIVE_BP_READ"]: "read",
}
HALT_CAUSES = {
    C["LIVE_HALT_BREAKPOINT"]: "breakpoint",
    C["LIVE_HALT_STEP"]: "step",
    C["LIVE_HALT_USER"]: "user",
    C["LIVE_HALT_RESET"]: "reset",
}
ERRORS = {
    C["LIVE_ERR_BADCMD"]: "unknown command",
    C["LIVE_ERR_BADLEN"]: "wrong payload length",
    C["LIVE_ERR_SPACE"]: "no such space, or not in the curated set",
    C["LIVE_ERR_RANGE"]: "address or length out of range",
    C["LIVE_ERR_REFUSED"]: "refused: would break the link",
    C["LIVE_ERR_UNSUP"]: "this target does not have that capability",
    C["LIVE_ERR_STATE"]: "wrong run state",
}


class ProtocolError(Exception):
    pass


class Nak(Exception):
    def __init__(self, cmd, err):
        self.cmd, self.err = cmd, err
        super().__init__(f"NAK on 0x{cmd:02X}: {ERRORS.get(err, err)}")


# --------------------------------------------------------------------------
# Codec — an independent implementation of include/live-frame.h.
# --------------------------------------------------------------------------


def build(cmd, payload=b""):
    """Encode one frame. Mirrors live_tx_build()."""
    payload = bytes(payload)
    if len(payload) > MAX_PAYLOAD:
        raise ValueError(f"payload {len(payload)} > {MAX_PAYLOAD}")
    body = bytes([len(payload), cmd]) + payload
    return bytes([SOF]) + body + bytes([(-sum(body)) & 0xFF])


class Decoder:
    """Streaming decoder. Mirrors live_rx_byte(), including its resync."""

    HUNT, LEN, CMD, DATA, SUM = range(5)

    def __init__(self):
        self.reset()

    def reset(self):
        self.st = self.HUNT
        self.len = 0
        self.cmd = 0
        self.buf = bytearray()
        self.sum = 0

    def idle(self):
        """Call on an inter-frame gap. Mirrors live_rx_idle().

        Without this, one truncated frame costs two: the decoder is stranded
        mid-payload and eats the next frame's header as payload.
        """
        self.st = self.HUNT
        self.buf = bytearray()

    def feed(self, data):
        """Yield (cmd, payload) for each complete, checksummed frame."""
        for b in data:
            if self.st == self.HUNT:
                if b == SOF:
                    self.st = self.LEN
            elif self.st == self.LEN:
                if b > MAX_PAYLOAD:
                    self.st = self.LEN if b == SOF else self.HUNT
                else:
                    self.len, self.sum = b, b
                    self.buf = bytearray()
                    self.st = self.CMD
            elif self.st == self.CMD:
                self.cmd = b
                self.sum = (self.sum + b) & 0xFF
                self.st = self.DATA if self.len else self.SUM
            elif self.st == self.DATA:
                self.buf.append(b)
                self.sum = (self.sum + b) & 0xFF
                if len(self.buf) == self.len:
                    self.st = self.SUM
            else:
                good = ((self.sum + b) & 0xFF) == 0
                self.st = self.HUNT
                if good:
                    yield self.cmd, bytes(self.buf)


# --------------------------------------------------------------------------
# Position, as boundary D §2 defines it.
# --------------------------------------------------------------------------


class Position:
    def __init__(self, blob):
        if len(blob) < 6:
            raise ProtocolError("position blob too short")
        self.running = blob[0] == C["LIVE_ST_RUNNING"]
        self.ntasks = blob[1]
        self.bw_ms = (blob[2] << 8) | blob[3]
        self.skew_ms = (blob[4] << 8) | blob[5]
        self.tasks = []
        need = 6 + 4 * self.ntasks
        if len(blob) < need:
            raise ProtocolError(f"position blob wants {need} bytes, got {len(blob)}")
        for i in range(self.ntasks):
            o = 6 + 4 * i
            state = (blob[o] << 8) | blob[o + 1]
            until = (blob[o + 2] << 8) | blob[o + 3]
            self.tasks.append((state, until))

    def __str__(self):
        where = []
        for i, (state, until) in enumerate(self.tasks):
            if state == 0xFFFF:
                where.append(f"task{i}: done")
            else:
                where.append(f"task{i}: state {state} until {until}")
        return (
            f"{'running' if self.running else 'HALTED':>7}  "
            f"bw_ms={self.bw_ms:<6} skew={self.skew_ms:<5}  " + "  ".join(where)
        )


RESOURCES = {
    "LIVE_RES_TIMER0": "Timer 0 (the millisecond tick)",
    "LIVE_RES_TIMER1": "Timer 1 (wall clock behind skew) \u2014 so a TONE pin cannot work",
    "LIVE_RES_TIMER2": "Timer 2 (baud rate on the STC15)",
    "LIVE_RES_BRT": "the baud-rate timer (baud rate on the STC12)",
    "LIVE_RES_UART1": "UART1 on P3.0/P3.1 \u2014 also the ISP pins",
    "LIVE_RES_PCA": "the PCA",
}


class Capabilities:
    def __init__(self, blob):
        if len(blob) < 8:
            raise ProtocolError("HELLO blob too short")
        self.version = blob[0]
        self.max_payload = blob[1]
        self.steps = [n for b, n in STEP_NAMES.items() if blob[2] & (1 << b)]
        self.breakpoints = [n for b, n in BP_NAMES.items() if blob[3] & (1 << b)]
        self.readable = [n for v, n in SPACE_NAMES.items() if blob[4] & (1 << v)]
        self.writable = [n for v, n in SPACE_NAMES.items() if blob[5] & (1 << v)]
        self.time_freezes = bool(blob[6] & C["LIVE_FLAG_TIME_FREEZES"])
        self.pc_valid = bool(blob[6] & C["LIVE_FLAG_PC_VALID"])
        self.sfrs_all = bool(blob[6] & C["LIVE_FLAG_SFRS_ALL"])
        self.max_tasks = blob[7]
        # Byte 8 is newer than the first firmware; treat its absence as "the
        # monitor does not say", not as "the monitor takes nothing".
        self.resources = None if len(blob) < 9 else [
            text for name, text in RESOURCES.items()
            if blob[8] & C.get(name, 0)
        ]

    def report(self):
        return "\n".join(
            [
                f"protocol version   {self.version}",
                f"max payload        {self.max_payload}",
                f"step kinds         {', '.join(self.steps) or '(none)'}",
                f"breakpoint kinds   {', '.join(self.breakpoints) or '(none)'}",
                f"readable spaces    {', '.join(self.readable)}",
                f"writable spaces    {', '.join(self.writable)}",
                f"halting freezes    {'program time' if self.time_freezes else 'nothing'}",
                f"PC reported        {'yes' if self.pc_valid else 'no (position is (task, state))'}",
                f"SFR window         {'all 256' if self.sfrs_all else 'curated set'}",
                f"max tasks          {self.max_tasks}",
            ]
            + (["peripherals taken  (not reported by this firmware)"]
               if self.resources is None else
               [f"peripherals taken  {self.resources[0] if self.resources else '(none)'}"]
               + [f"                   {r}" for r in self.resources[1:]])
        )


# --------------------------------------------------------------------------
# The target.
# --------------------------------------------------------------------------


class LiveTarget:
    def __init__(self, port, baud=115200, timeout=1.0):
        import serial  # imported lazily: --selftest must not need pyserial

        self.ser = serial.Serial(port, baud, timeout=timeout)
        self.dec = Decoder()
        self.pending_halts = []

    def _exchange(self, cmd, payload=b"", expect=None):
        self.ser.write(build(cmd, payload))
        want = expect if expect is not None else (cmd | 0x80)
        deadline = time.time() + 2.0
        while time.time() < deadline:
            data = self.ser.read(64)
            if not data:
                self.dec.idle()     # a read timeout IS an inter-frame gap
                continue
            for rcmd, rpay in self.dec.feed(data):
                if rcmd == C["LIVE_EVT_HALT"]:
                    self.pending_halts.append((rpay[0], Position(rpay[1:])))
                    continue
                if rcmd == C["LIVE_NAK"]:
                    raise Nak(rpay[0], rpay[1])
                if rcmd == want:
                    return rpay
        raise ProtocolError(f"no reply to 0x{cmd:02X} within 2 s")

    def hello(self):
        return Capabilities(self._exchange(C["LIVE_CMD_HELLO"]))

    def read(self, space, addr, length):
        s = SPACES[space] if isinstance(space, str) else space
        return self._exchange(
            C["LIVE_CMD_READ"], bytes([s, (addr >> 8) & 0xFF, addr & 0xFF, length])
        )

    def write(self, space, addr, data):
        s = SPACES[space] if isinstance(space, str) else space
        return self._exchange(
            C["LIVE_CMD_WRITE"],
            bytes([s, (addr >> 8) & 0xFF, addr & 0xFF]) + bytes(data),
        )[0]

    def regs(self):
        b = self._exchange(C["LIVE_CMD_REGS"])
        names = ["A", "B", "DPL", "DPH", "SP", "PSW", "bank"]
        out = dict(zip(names, b[:7]))
        for i in range(8):
            out[f"R{i}"] = b[7 + i]
        return out

    def position(self):
        return Position(self._exchange(C["LIVE_CMD_POS"]))

    def run(self):
        return self._exchange(C["LIVE_CMD_RUN"])[0]

    def halt(self):
        self._exchange(C["LIVE_CMD_HALT"])
        return self.wait_halt()

    def step(self, count=1, kind="block"):
        kinds = {v: k for k, v in STEP_NAMES.items()}
        self._exchange(C["LIVE_CMD_STEP"], bytes([kinds[kind], count]))
        return self.wait_halt()

    def set_breakpoint(self, task, state, kind="yield"):
        kinds = {v: k for k, v in BP_NAMES.items()}
        return self._exchange(
            C["LIVE_CMD_BPSET"],
            bytes([kinds[kind], task, (state >> 8) & 0xFF, state & 0xFF]),
        )[0]

    def clear_breakpoint(self, handle):
        return self._exchange(C["LIVE_CMD_BPCLR"], bytes([handle]))[0]

    def symbols(self, entries):
        """entries: [(space, addr)] — bw_ms, then state/until per task."""
        ntasks = (len(entries) - 1) // 2
        pay = bytearray([ntasks])
        for space, addr in entries:
            s = SPACES[space] if isinstance(space, str) else space
            pay += bytes([s, (addr >> 8) & 0xFF, addr & 0xFF])
        return self._exchange(C["LIVE_CMD_SYMS"], bytes(pay))[0]

    def wait_halt(self, timeout=3.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.pending_halts:
                return self.pending_halts.pop(0)
            data = self.ser.read(64)
            if not data:
                self.dec.idle()
                continue
            for rcmd, rpay in self.dec.feed(data):
                if rcmd == C["LIVE_EVT_HALT"]:
                    return rpay[0], Position(rpay[1:])
        raise ProtocolError("no halt event")


# --------------------------------------------------------------------------
# Self-test — the codec, with no hardware and no pyserial.
# --------------------------------------------------------------------------


def selftest(vectors_from=None):
    checks = failures = 0

    def ok(cond, what):
        nonlocal checks, failures
        checks += 1
        if not cond:
            failures += 1
            print(f"  FAIL {what}")

    print("round trip, every payload length 0..%d" % MAX_PAYLOAD)
    for n in range(MAX_PAYLOAD + 1):
        payload = bytes((i * 7 + n) & 0xFF for i in range(n))
        frame = build(C["LIVE_CMD_READ"], payload)
        ok(len(frame) == n + 4, f"frame length for n={n}")
        ok(sum(frame[1:]) % 256 == 0, f"checksum property for n={n}")
        got = list(Decoder().feed(frame))
        ok(got == [(C["LIVE_CMD_READ"], payload)], f"decode for n={n}")

    print("corruption is rejected")
    frame = bytearray(build(C["LIVE_CMD_WRITE"], bytes(range(0x10, 0x18))))
    frame[5] ^= 0x01
    ok(list(Decoder().feed(frame)) == [], "flipped payload byte rejected")
    frame[5] ^= 0x01
    frame[-1] ^= 0x80
    ok(list(Decoder().feed(frame)) == [], "flipped checksum rejected")
    frame[-1] ^= 0x80
    ok(len(list(Decoder().feed(frame))) == 1, "intact frame accepted")

    print("resynchronisation after garbage")
    junk = bytes([0x00, 0xFF, 0xAA, 0x7E, 0xFF, 0x55, 0x7E])
    good = build(C["LIVE_CMD_POS"], b"\x01\x02\x03\x04")
    d = Decoder()
    ok(list(d.feed(junk)) == [], "garbage yields nothing")
    ok(len(list(d.feed(good))) == 1, "good frame after garbage")
    d = Decoder()
    ok(list(d.feed(good[:-2])) == [], "truncated frame yields nothing")
    ok(list(d.feed(good)) == [], "truncation eats the next frame too")
    ok(len(list(d.feed(good))) == 1, "and the one after that decodes")

    d = Decoder()
    ok(list(d.feed(good[:-2])) == [], "truncated frame yields nothing")
    d.idle()
    ok(len(list(d.feed(good))) == 1, "after an idle gap, no frame is lost")

    print("a payload of SOF bytes needs no escaping")
    sofs = bytes([SOF] * 5)
    ok(
        list(Decoder().feed(build(C["LIVE_CMD_WRITE"], sofs)))
        == [(C["LIVE_CMD_WRITE"], sofs)],
        "0x7E payload survives",
    )

    print("blob parsers")
    halted = C["LIVE_ST_HALTED"]
    pos = Position(
        bytes([halted, 2, 0x01, 0x2C, 0x00, 0x05, 0, 3, 0x01, 0x2C, 0xFF, 0xFF, 0, 0])
    )
    ok(not pos.running and pos.bw_ms == 300 and pos.skew_ms == 5, "position header")
    ok(pos.tasks == [(3, 300), (0xFFFF, 0)], "per-task position")
    cap = Capabilities(bytes([1, 64, 0x04, 0x02, 0x1F, 0x1E, 0x01, 4]))
    ok(cap.steps == ["block"], "only block stepping is offered")
    ok(cap.breakpoints == ["yield"], "only yield breakpoints are offered")
    ok(cap.time_freezes and not cap.pc_valid and not cap.sfrs_all, "honest flags")

    if vectors_from:
        print(f"cross-checking the C encoder ({vectors_from})")
        try:
            out = subprocess.run(
                [vectors_from, "--vectors"], capture_output=True, text=True, check=True
            ).stdout
        except (OSError, subprocess.CalledProcessError) as exc:
            print(f"  FAIL could not run vector generator: {exc}")
            failures += 1
            out = ""
        for line in out.splitlines():
            cmd_hex, pay_hex, frame_hex = line.split()
            cmd = int(cmd_hex, 16)
            payload = b"" if pay_hex == "-" else bytes.fromhex(pay_hex)
            mine = build(cmd, payload).hex().upper()
            ok(mine == frame_hex, f"C and Python agree on 0x{cmd:02X} ({frame_hex})")
            got = list(Decoder().feed(bytes.fromhex(frame_hex)))
            ok(got == [(cmd, payload)], f"Python decodes the C frame 0x{cmd:02X}")

    print(f"\n{checks} checks, {failures} failures")
    return 1 if failures else 0


# --------------------------------------------------------------------------


def attach(args):
    t = LiveTarget(args.port, args.baud)
    cap = t.hello()
    print(cap.report())
    print()

    if not args.watch:
        print(t.position())
        return 0

    print("watching position; ctrl-c to stop")
    try:
        while True:
            print("\r" + str(t.position()) + " " * 8, end="", flush=True)
            time.sleep(0.2)
    except KeyboardInterrupt:
        print()
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument("--port", help="serial device, e.g. /dev/cu.usbserial-1420")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--watch", action="store_true", help="live position display")
    ap.add_argument("--selftest", action="store_true", help="codec tests, no hardware")
    ap.add_argument("--vectors-from", help="path to the C frame_test binary")
    args = ap.parse_args()

    if args.selftest:
        return selftest(args.vectors_from)
    if not args.port:
        ap.error("need --port or --selftest")
    return attach(args)


if __name__ == "__main__":
    sys.exit(main())
