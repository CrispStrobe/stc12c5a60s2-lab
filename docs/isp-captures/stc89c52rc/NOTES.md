# STC89C52RC ISP captures — bench notes (2026-08-17, evening)

Bench-capture role per `../../STC-ISP-CLEANROOM.md`. Verbatim stcgal
debug logs of real sessions against the owner's silicon; every log in
this directory is an unedited `stdout+stderr` capture of exactly the
command line named below. Power was cycled by hand for each session
(the board's power switch); DTR autoreset was tried once and does NOT
reach this board's power rail (see `00-autoreset-attempt.log`).

## Environment

- stcgal **1.10** (pipx install), invoked as `stcgal -D -P stc89 -p /dev/cu.usbserial-1110 …`
- macOS **26.2** (Darwin 25.2.0), Apple Silicon
- Adapter: the board's own **CH340C** (Prechin 普中51-单核-A2 dev board);
  device node `/dev/cu.usbserial-1110`
- Chip as reported by every session: **STC89C52RC/LE52RC**, Magic F002,
  8.0 KB code flash, 6.0 KB EEPROM flash, **BSL 6.6C**
- Reported target frequency varies per session between **10.973 MHz**
  and **11.030 MHz** (same board, same crystal, minutes apart) — the
  value is derived from the handshake pulse measurement, so this
  spread is a protocol fact, not a bench inconsistency.
- Baud: handshake at stcgal's default (2400), then the tool switches
  the session to 115200 ("Switching to 115200 baud") — the mid-session
  baud change is visible in every flash/erase log.

## Sessions (each captured twice for determinism)

| log | command (after `stcgal -D -P stc89 -p /dev/cu.usbserial-1110`) | result |
|---|---|---|
| `00-autoreset-attempt.log` | `-a` (autoreset probe) | DTR pulse sent, MCU never appeared; killed. Kept as evidence autoreset is inert on this board |
| `01-info-run1.log` / `run2` | *(no further args — info only)* | full handshake + identity, no payload |
| `02-erase-run1.log` / `run2` | `-e` | erase only |
| `03-flash-blink-run1.log` / `run2` | `build/stc89c52rc/01-blink/01-blink.hex` | small image, 640 bytes written |
| `04-flash-hello-run1.log` / `run2` | `build/stc89c52rc/04-hello89/04-hello89.hex` | larger image, multiple write packets |
| `05-timeout-nocycle.log` | *(no power cycle; SIGINT after 30 s)* | the waiting/pulse behavior against a silent target |

## Flashed image identities (sha256)

- `01-blink.hex` — `9984fa68119f32822196639a8b60dfccab5b0e945ee22c862e078252b63a9aeb`
  (built by this repo's Makefile stc89c52rc lane; both hexes are our own
  MIT code, so their bytes appearing inside write packets is fine)
- `04-hello89.hex` — `464ee03699c7d8175fa6d2d37a2517b2971c8db21d1321aec1d152f08fa0ffcf`

## Provenance statement (capture role)

This session ran the stcgal *binary* only. No stcgal source code and
no stcgal repository documentation were opened, read, or excerpted in
this session, and none is present in this directory or repo. The logs
are byte dumps of our wire to our chip: `<- Packet data:` lines are
MCU→host, `->` host→MCU, hex bytes verbatim from stcgal's own `-D`
output. Progress-bar lines contain `\r` carriage returns, preserved.

Two facts observed on the wire worth flagging to the spec role:

1. Both directions frame packets as `46 B9 … 16` with what appears to
   be a length field and a trailing checksum before `16`; the first
   MCU packet (the "status" packet) carries eight repeated 16-bit
   words early on (`0A 6B` ×8 in one session) that vary with the
   session's measured frequency — consistent with the handshake pulse
   measurement being reported by the chip itself.
2. The reported "Target frequency" differs between sessions on the
   same crystal (10.973 vs 11.030 MHz), so a reimplementation must
   treat frequency as measured per-handshake, not as a chip constant.
3. `05-timeout-nocycle.log` is better than designed: the chip was
   still RUNNING the hello firmware, which prints on the same UART —
   so the log shows stcgal receiving application bytes (framing
   garbage at handshake baud) while waiting. A reimplementation must
   reject non-`46 B9` noise on the line, not just silence.
