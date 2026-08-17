# `isp-capture` — normalizing ISP capture logs

`normalize.py` turns a raw capture log into a **frame table**: one JSON
object per byte run that crossed the wire, in order, tagged with
direction and session phase.

Two consumers share that table, which is why it exists at all:

- **`docs/STC-ISP-PROTOCOL.md`** — every byte-level claim in the spec is
  supposed to be walkable back to a specific record, hence the `src`
  field (`file:line`).
- **`tools/stcbsl`'s replay tests** — the frame tables in
  `docs/isp-captures/*/frames/` are committed fixtures. A replay test
  feeds the host frames to the implementation and asserts it produces the
  same bytes, and feeds the MCU frames back as the target's answers.

No third-party dependencies; `python3` only.

```bash
tools/isp-capture/normalize.py --selftest            # no files needed
tools/isp-capture/normalize.py docs/isp-captures/stc89c52rc/*.log \
    -o docs/isp-captures/stc89c52rc/frames/          # regenerate fixtures
tools/isp-capture/normalize.py some.log --stats      # phase/direction summary
tools/isp-capture/normalize.py some.log --strict     # non-zero if a frame fails validation
```

`-o` writes one `<name>.jsonl` per input when given a directory (a path
ending in `/`, or an existing one), otherwise everything to one file.
With no `-o` it prints to stdout.

## JSONL schema

One object per line. The first four keys are the contract; the rest are
provenance and convenience, and a consumer may ignore them.

| key | type | meaning |
|---|---|---|
| `phase` | string | session phase, see below |
| `dir` | string | `"host->mcu"` or `"mcu->host"` — the **sender** is named first |
| `bytes_hex` | string | the byte run, lowercase hex, space-separated |
| `note` | string | `;`-joined annotations, see below |
| `seq` | int | 1-based index within the source file |
| `src` | string | `basename:lineno` of the source log |
| `fmt` | string | which line format matched |
| `cmd` | string | command byte (`"0x8f"`), present only on well-formed frames |

Phases, derived from the protocol's command bytes (never from a host
tool's progress text, so they mean the same thing across capture tools):

`sync` · `status` · `baud_probe` · `baud_commit` · `link_test` ·
`erase` · `write` · `options` · `run`

A record inherits the current phase; a host frame whose command byte is
known *sets* it, so the MCU's reply lands in the same phase as the
command it answers. Everything before the first frame is `sync`. A log
may override the phase explicitly with a comment line:

```
# phase: whatever-you-want
```

Note vocabulary:

- `frame-ok` — magic, length, checksum and terminator all validate.
- `not-a-frame`, `noise` — a byte run that is not a `46 B9 …` frame.
  These are kept deliberately: the noise before the handshake is
  evidence (see the spec's noise-rejection section), not clutter.
- `bad-terminator=…`, `bad-length declared=… actual=…`,
  `bad-checksum computed=… got=…`, `unknown-dir-byte=…`,
  `direction-marker-disagrees-with-byte …` — a frame that parsed
  structurally but failed a check. **Still emitted.** Dropping a
  malformed frame would delete exactly the evidence a protocol bug lives
  in.
- `text=…` — the capturing tool's own message that preceded the bytes on
  that line, preserved verbatim for context.

## Line formats

Formats are tried in order; the first that matches a line wins, so one
file may mix them.

| name | shape | status |
|---|---|---|
| `stcgal_debug` | `<text> <- Packet data: 46 B9 …` | the STC89C52RC bench captures |
| `labeled_ts` | `[ 4.530501 ] mcu: 0x46 0xb9 …` | timestamped sniffer logs |
| `arrow_hex` | `-> 46 b9 …`, `RX: …` | bare direction markers |
| `pty_tap` | — | **TODO**, needs per-format state |
| `logic_analyzer` | — | **TODO**, sigrok/PulseView CSV |

Adding one is a single function returning
`(direction, bytes, leading_text)` plus an entry in `FORMATS`. The two
TODO stubs document what each will need; the logic-analyzer one is the
route to capturing the host's `0x7F` pulse train, which no host-side
tool can show us because the tool does not log its own sync bytes.

Direction is decided by the *format's* marker, and then cross-checked
against the frame's own direction byte; a disagreement is recorded in
`note` rather than silently resolved. A sniffer log with its labels
swapped is a real failure mode and one worth catching loudly.

## Regenerating

The fixtures in `docs/isp-captures/*/frames/` are committed and must stay
in sync with the `.log` files beside them. After adding or re-taking a
capture:

```bash
tools/isp-capture/normalize.py docs/isp-captures/stc89c52rc/*.log \
    -o docs/isp-captures/stc89c52rc/frames/
tools/isp-capture/normalize.py --selftest
```

The raw `.log` files are the primary evidence; the `.jsonl` files are
derived and may be regenerated at will. If the two ever disagree, the
`.log` wins.
