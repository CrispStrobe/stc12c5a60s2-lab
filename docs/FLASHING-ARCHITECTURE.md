# Flashing & CLI architecture — one transpiler, two flash engines, three homes

The decision record behind "a universal cross-platform app whose harness is
Rust/Tauri," and the answer to two owner questions (2026-08-25):

1. *Can we have both shims and native Rust, user-selectable, in GUI and CLI?*
   **Yes** — and the two implementations check each other.
2. *Can the Rust binary have all the features the node `bw` CLI has?*
   **Yes — hardware natively; transpile by embedding the JS engine (the ONE
   transpiler), not by reimplementing it and not via Python. Done: `bwc`.**

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

## Decision 2 — ONE transpiler, the JS SB3Creator, run in a JS host

The `bw` commands split in two:

- **Transpile** (`transpile`, `retarget`, `devices`, `check`, `decompile`,
  and `compile`'s pseudocode→C step): the JS **SB3Creator** engine (~15k
  lines) — the crown jewel.
- **Hardware** (`flash`, `read`): serial/USB byte-pushing.

There is exactly **one** transpiler, and it is the JS `SB3Creator`. It is
never reimplemented in Rust and never replaced by a second engine — it runs
in a **JS host**, whichever one the home provides:

| Home | JS host that runs SB3Creator |
|---|---|
| Web app | the browser |
| Tauri app | the system webview |
| node `bw` | node |
| **Rust CLI** | **an embedded JS engine INSIDE the binary** — `rquickjs` (QuickJS, MIT, ~1 MB) or `boa` (pure-Rust, MIT) |

So a self-contained Rust CLI is real without giving up the JS transpiler: the
binary embeds QuickJS, evals a bundled `SB3Creator.js` (esbuild → one file →
`include_str!`), and calls `retargetPseudocode` / `generateC` in-process. No
node subprocess, no network, no Python, no drift — the SAME transpiler the
browser runs. C→binary afterwards is gcc/sdcc/cc65 (local toolchains, shelled
to as `bw compile` already does), or the hosted service's C-only `/compile`
(that step is compilation, not transpilation, and does not drift).

### The Python transpiler on the hosted service is a dead end — retire it

Found while verifying (2026-08-25): the hosted `stc-compiler` service has a
SECOND transpiler, `stc_pseudocode.py` (Python), reached by
`language:"pseudocode"`. It has drifted — it does not know `STM32F030` or the
recent ADC/PWM/retarget work — and it CANNOT be the answer, for the reason the
owner stated: **you cannot embed Python inside a Rust binary.** It is not the
canonical engine and must not be made one. The browser and node `bw` already
ignore it (they transpile with JS SB3Creator and send C to the service). The
Rust CLI ignores it too, by embedding the JS. `stc_pseudocode.py` should be
retired as a transpiler, or at most kept as the service's own convenience for
`curl`, never a source of truth.

## What this makes possible

- **Universal Tauri app**: webview runs SB3Creator (transpile) + `flash.js` over
  Rust shims (flash); Settings picks JS-shim or native-Rust flashing.
- **node `bw` CLI**: transpiles locally (SB3Creator), flashes via `flasher.js`
  or `--engine rust`.
- **Rust CLI** (or `stcbsl` grown into one): transpiles by running the JS
  SB3Creator in an EMBEDDED QuickJS engine, flashes natively — a single
  self-contained binary that carries the same transpiler, for CI and benches.

All three share: one transpiler (JS SB3Creator, run in each home's JS host —
an embedded QuickJS for a Rust binary), one flash-protocol spec (mirrored in JS and Rust, differentially
checked), and the fully-permissive rule — no GPL tool bundled or invoked
anywhere (avrdude is named only as the user's own optional tool).

## Build order (each a landable slice)

1. **DONE** — `bw flash --engine js|rust` (node dispatch to `stcbsl`/`stm32bsl`).
   sb3-creator `main`.
2. Binary + parity + DTR Rust serial commands + `tauriSerialPort()` shim. — the
   serial flashers light up in the app.
3. USB Rust commands (`nusb`) + `tauriUsbDevice()` shim. — USBasp/SWD in the app.
4. **DONE** — embedded QuickJS (`rquickjs`) transpiler in the Rust CLI. The JS
   side is `sb3-creator` `src/embed/` + `scripts/bundle-embed.mjs` (bare-engine
   bundle, byte-identical to node, CI-guarded); the Rust side is
   `tools/stcbsl/src/transpile.rs` + the `bwc` binary, with the bundle vendored
   and drift-checked by `sync-transpiler.mjs`. `bwc <file.bw>` transpiles
   pseudocode → C in-process — no node, no network, no Python. This is where the
   earlier wrong turn (the service's Python transpiler) was corrected.
5. Grow the Rust CLI into the full flash family + a `compile` subcommand
   (C→binary via a local toolchain or the service's C-only compile), unifying
   `bwc` + `stcbsl` + `stm32bsl` under one self-contained `bw`.
