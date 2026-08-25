# Flashing & CLI architecture — one transpiler, two flash engines, three homes

The decision record behind "a universal cross-platform app whose harness is
Rust/Tauri," and the answer to two owner questions (2026-08-25):

1. *Can we have both shims and native Rust, user-selectable, in GUI and CLI?*
   **Yes** — and the two implementations check each other.
2. *Can the Rust binary have all the features the node `bw` CLI has?*
   **Hardware yes; transpile via the hosted service, not reimplemented.**

---

## The three homes for one codebase

Brickwright is not three products. It is one web app (the scratch-gui build)
that runs in three places, each differing only in how it reaches hardware:

| Home | What it is | Hardware transport |
|---|---|---|
| **Web app** (GitHub Pages, Chrome/Edge) | pure browser | Web Serial / WebUSB (native, Chromium-only) |
| **`bw` CLI** | node, a standalone dev tool | `nodeSerialPort` (POSIX raw fd) |
| **Tauri app** (mac/win/linux/ios/android) | Rust harness + system webview | Rust `#[tauri::command]`s via `invoke()` |

**node is never inside the Tauri app.** The app is Rust + a webview; the CLI
is a separate node tool. They share code because the flashers are written
*transport-agnostic* — `flash.js`'s functions take a `port`/transport object
and never touch `navigator` themselves, so the same protocol runs over a Web
Serial port, a raw fd, or a Rust-backed shim.

The system webview (WKWebView on macOS/iOS, WebKitGTK on Linux) has **no Web
Serial / WebUSB** — they are Chromium-only. That is *why* the Tauri app must
reach hardware through Rust, and why `pico-tauri-transport.js` already exists:
the same `picoRepl` codec, its `{write,read,close}` backed by Rust
`serialport` commands instead of Web Serial.

---

## Decision 1 — two flash engines, user-selectable

The flash *protocol* lives in **both** JS (`flash.js`, six paths: STC ISP,
STM32 AN3155, STK500 v1/v2, EEPROM/bweep, USBasp ISP, CMSIS-DAP SWD) and
**Rust** (`stcbsl`/`stm32bsl` today; grow to the rest). The user chooses:

- **GUI (Tauri):** a Settings toggle — *Flash engine: Auto / Browser (JS) /
  Native (Rust)*. "Browser" runs `flash.js` over a Rust byte-pipe shim;
  "Native" calls a single Rust flash command. The pure web app is JS-only.
- **CLI:** `bw flash --engine js|rust`. `js` = `flasher.js` + `nodeSerialPort`;
  `rust` = shell to the Rust binary.

This is **not** wasteful duplication. Two independent implementations that
must agree byte-for-byte are a **differential oracle** — the same habit the
fleet uses for labwired vs. the F0 machine. Each catches the other's bugs, and
a bench capture from either reads the same (the flashers already share the
AN3155 ACK/NACK byte values for exactly this reason).

**Transport shims the Tauri side needs** (the one real gap):
- The existing Rust `serial_write/read` are **String-based** (fine for the
  Pico's text REPL). Flashing needs **binary** (base64 or byte arrays),
  **parity** (STM32 is 8E1), and **DTR** (`set_signals`, for AVR bootloader
  reset). A `tauriSerialPort()` shim then presents the Web-Serial shape.
- USBasp/SWD need **USB transport commands** (`nusb` — pure-Rust, no libusb —
  or `rusb`/LGPL libusb; both permissive) and a `tauriUsbDevice()` shim.

---

## Decision 2 — the transpiler is ONE engine, reached over the service

The `bw` commands split in two:

- **Transpile** (`transpile`, `retarget`, `devices`, `check`, `decompile`,
  and `compile`'s pseudocode→C step): the JS **SB3Creator** engine (~15k
  lines) — the crown jewel.
- **Hardware** (`flash`, `read`): serial/USB byte-pushing.

A pure-Rust CLI **cannot** hold the transpiler without reimplementing it
(enormous, unjustified). So it does not: it **calls the hosted service**, which
already exposes `/compile` and `/transpile` and accepts `language:
"pseudocode"`. A Rust CLI sends raw pseudocode + target + format and gets a
binary back — full features, no JS, no SB3Creator dependency in Rust.

### The catch found while verifying this (2026-08-25): TWO transpilers, drifted

The service transpiles pseudocode with its **own Python** engine
(`stc_pseudocode.py`), NOT the JS SB3Creator. They have drifted: the Python
one does **not** know `STM32F030` (or the recent ADC/PWM/retarget work), while
SB3Creator does. So today:

- Browser app & node `bw`: transpile with **JS SB3Creator** (canonical,
  current) → send C to the service → binary. Works for every target.
- `language:"pseudocode"` on the service: uses the **stale Python** transpiler.
  Misses STM32F030 and anything added to SB3Creator since the Python copy last
  tracked it.

**For "Rust CLI (or any client) calls the service with pseudocode" to give
full, current features, the service's transpile must be the canonical
engine.** Options, cleanest first:

1. **Run SB3Creator on the service** — a node serverless function alongside the
   Python app (Vercel supports both), so `language:"pseudocode"` runs the SAME
   engine the browser uses. One source of truth; the Rust CLI and the app get
   identical, current transpile. Retires `stc_pseudocode.py` as a transpiler.
2. **Keep `stc_pseudocode.py` and sync it to SB3Creator** — dual maintenance,
   the exact drift CLAUDE.md warns about (the sb3-creator vendor lesson). Not
   recommended.
3. **Rust CLI sends C, transpiling elsewhere** — but Rust can't run SB3Creator,
   so "elsewhere" is node or the service anyway. Circular.

**Recommendation: option 1.** The service becomes the one place any headless
client (Rust CLI, curl, CI) turns pseudocode into a binary, using the same
transpiler the GUI uses.

---

## What this makes possible

- **Universal Tauri app**: webview runs SB3Creator (transpile) + `flash.js` over
  Rust shims (flash); Settings picks JS-shim or native-Rust flashing.
- **node `bw` CLI**: transpiles locally (SB3Creator), flashes via `flasher.js`
  or `--engine rust`.
- **Rust CLI** (or `stcbsl` grown into one): transpiles by calling the service
  (once it is canonical), flashes natively — a single self-contained binary
  with no JS, for CI and headless benches.

All three share: one transpiler (SB3Creator, on the service for headless
clients), one flash-protocol spec (mirrored in JS and Rust, differentially
checked), and the fully-permissive rule — no GPL tool bundled or invoked
anywhere (avrdude is named only as the user's own optional tool).

## Build order (each a landable slice)

1. `bw flash --engine js|rust` (node dispatch to `stcbsl`/`stm32bsl`). — small
2. Binary + parity + DTR Rust serial commands + `tauriSerialPort()` shim. — the
   serial flashers light up in the app.
3. USB Rust commands (`nusb`) + `tauriUsbDevice()` shim. — USBasp/SWD in the app.
4. SB3Creator on the service (node function), canonical pseudocode→binary. —
   unblocks a full Rust CLI and retires the drift.
5. Grow `stcbsl` into the full flash family + a `compile` subcommand that calls
   the service. — the self-contained Rust CLI.
