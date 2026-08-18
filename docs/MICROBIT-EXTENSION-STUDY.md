# micro:bit gallery extension — concept study & design

**Scope.** This is the research + design that fleshes out
[`MICROBIT-NATIVE.md`](MICROBIT-NATIVE.md) **§1a** ("Our own micro:bit gallery
extension — beyond legacy"). It is a *study and a design*, not an
implementation: it inventories what the stock Scratch micro:bit extension does,
inventories the far richer "micro:bit More" feature envelope, extracts the one
structural idea that makes that envelope affordable (sensor **streaming**), and
proposes the concrete block set for a BrickWright extension (`microbitPlus`)
built on our own conventions. A reader should be able to start implementing v1
from §5.

**Licence discipline is load-bearing here** and is recorded per source, exactly
as `MICROBIT-NATIVE.md §0` requires:

| source | licence | how this study uses it |
|---|---|---|
| stock `scratch3_microbit` | **BSD-3** (bundled, pinned) | baseline only — establishes "beyond legacy" |
| `microbit-more/mbit-more-v2` (Scratch extension) | **MIT** © 2020–2022 Koji Yokokawa | read + adaptable **with attribution**; exact identifiers may be quoted |
| `microbit-more/pxt-mbit-more-v2` (firmware) | **NONE — all rights reserved** | **clean-room concepts only** — behaviour/shape paraphrased, no code, provenance recorded (§6b) |

The full provenance split — what is safe to adapt vs what must be re-derived — is
**§6**, and it is the reason this document exists in the shape it does.

---

## 1. Baseline gap — what "beyond legacy" means

The stock `scratch3_microbit` extension (bundled, BSD-3;
`packages/scratch-vm/src/extensions/scratch3_microbit/index.js`) is deliberately
thin. It exposes **nine blocks** total and streams a **single fixed 10-byte
packet**. Here is the whole of it against what the hardware can actually do:

| capability | stock exposes | stock omits |
|---|---|---|
| **buttons** | `when [A/B/any] pressed` (hat), `[A/B/any] pressed?` (bool) | up/down/click distinction, hold/double-click, logo touch (v2) |
| **display** | `display [matrix]`, `display text`, `clear` | per-pixel *brightness* (on/off only), scroll-delay control |
| **accelerometer** | `when tilted [dir]`, `tilted [dir]?`, `tilt angle [dir]` (front/back/left/right) | raw x/y/z, pitch/roll as numbers, gesture set beyond move/shake/jump |
| **gesture** | `when [moved/shaken/jumped]` (3 only) | tilt-up/down/left/right, face-up/down, freefall, 3g/6g/8g (8 more) |
| **pins — touch** | `when pin [0/1/2] connected` (hat) | everything else about pins |
| **pins — digital** | — | **digital read, digital write** |
| **pins — analog in** | — | **analog read (0–1023)** on P0/P1/P2 |
| **pins — analog out** | — | **PWM / analog write** |
| **pins — mode** | — | **pull up/down/none, edge/pulse event arming** |
| **magnetometer / compass** | — | **heading, raw field x/y/z** |
| **light sensor** | — | **light level** |
| **temperature** | — | **on-die temperature** |
| **sound (v2 mic)** | — | **sound level** |
| **tone / music** | — | **play tone, stop tone** (v1 needs a pin speaker; v2 has one) |
| **servo** | — | **angle + continuous** |
| **radio / messaging** | — | **labelled number/text messages between boards** |
| **serial / UART** | — | (transport only, not a block surface) |

**Size of the gap.** Stock = **9 blocks, 1 sensor packet, 3 device commands**
(`CMD_PIN_CONFIG`, `CMD_DISPLAY_TEXT`, `CMD_DISPLAY_LED`). The "More" envelope
(§2) is **~30 blocks, 3 sensor frames + 3 event channels + per-pin analog, and a
5-category command set**. "Beyond legacy" is not a polish pass — it is roughly a
**3× block count** and a categorically different protocol (streaming vs a single
fixed packet). Everything below defines that target and how we hit it on our own
terms.

---

## 2. Feature inventory — the "micro:bit More" envelope

This is the full capability set the MIT `mbit-more-v2` extension exposes
(extension id `microbitMore`, "MicroBit More"; blocks declared in
`getInfo()` of `src/vm/extensions/block/index.js`). Grouped by axis, with the
**block shape** (command / reporter / boolean / hat) and argument types. This is
the reference envelope; §5 is *our* redesign of it.

Legend for block shape: **C** = command, **R** = reporter, **B** = boolean,
**H** = hat.

### 2.1 Pins / GPIO
| block text | shape | args |
|---|---|---|
| set [PIN] digital [LOW/HIGH] | C | PIN menu (gpio), LEVEL menu |
| set [PIN] analog [0–100] % | C | PIN menu, NUMBER (percent → PWM) |
| set [PIN] servo [angle] | C | PIN menu, NUMBER angle (range/center hard-defaulted) |
| set pin [PIN] to input [none/up/down] | C | PIN menu, MODE menu (pull) |
| [PIN] pin is high? | B | PIN menu |
| analog value of pin [P0/P1/P2] | R | PIN menu (analogIn only) → 0–100.0 |
| listen [none/pulse/edge] event on [PIN] | C | EVENT_TYPE menu, PIN menu |
| when catch [low pulse/high pulse/fall/rise] at pin [PIN] | H | EVENT menu, PIN menu |
| value of [event] at [PIN] | R | EVENT menu, PIN menu (edge timestamp / pulse width) |

`gpio` menu is the usable edge-connector set **{0,1,2,8,12,13,14,15,16}**;
`analogIn` is **{0,1,2}**.

### 2.2 Sensors (all reporters)
| block text | shape | args / range |
|---|---|---|
| acceleration [x/y/z] | R | AXIS menu → milli-g |
| pitch | R | degrees |
| roll | R | degrees |
| magnetic force [x/y/z/absolute] | R | AXIS menu → µT |
| angle with the North (compass heading) | R | 0–359 |
| light intensity | R | 0–100.0 (from 0–255) |
| temperature | R | °C |
| sound level | R | 0–100.0 (v2 mic; lazily enabled) |

### 2.3 Actuators (commands)
| block text | shape | args |
|---|---|---|
| play tone [freq] Hz volume [vol] % | C | FREQ NUMBER, VOL NUMBER |
| stop tone | C | — |
| set [PIN] servo [angle] | C | (also listed under pins) |

### 2.4 Display (commands)
| block text | shape | args |
|---|---|---|
| display pattern [MATRIX] | C | ArgumentType.MATRIX (25-cell grid) |
| display text [TEXT] delay [DELAY] ms | C | STRING, NUMBER |
| clear display | C | — |

### 2.5 Events (hats + booleans)
| block text | shape | args |
|---|---|---|
| when button [A/B] is [down/up/click] | H | NAME menu, EVENT menu |
| button [A/B] pressed? | B | NAME menu |
| when pin [LOGO/P0/P1/P2] is [touched/released/tapped] | H | NAME menu, EVENT menu |
| pin [name] is touched? | B | NAME menu |
| when [gesture] | H | GESTURE menu (11 gestures) |
| when micro:bit [connected/disconnected] | H | STATE menu |

Gesture set: tilt up/down/left/right, face up/down, freefall, 3g/6g/8g, shake
(**11**, vs stock's 3). Button/touch event set: down/up/click active
(hold/long-click/double-click present in the enum but disabled as unstable).

### 2.6 Comms — labelled messaging (the "radio" analogue)
mbit-more has **no radio-group/power block set**. Inter-device messaging is a
**labelled-data channel** relayed by the firmware:

| block text | shape | args |
|---|---|---|
| when data with label [LABEL] received from micro:bit | H | LABEL STRING |
| data of label [LABEL] | R | LABEL STRING |
| send data [DATA] with label [LABEL] to micro:bit | C | LABEL STRING, DATA STRING |

Numbers ride as `float32`, text as ≤11 ASCII bytes, under an 8-byte label
(§3.3). Radio **group** and **TX power** are simply *not modelled* — a real gap
we should close (§5).

### 2.7 Comms — serial / I²C / SPI
Serial is a **transport** (an alternate pipe to the same protocol, §4), **not a
user-facing block set**. There are **no I²C or SPI blocks** in mbit-more. Those
remain open design space for us (§5, "later").

**Inventory total:** ~30 blocks across 7 groups (pins 9, sensors 8, actuators 3,
display 3, events 6, messaging 3), plus connection state.

---

## 3. Protocol structure — streaming, not per-block polling

The single most important idea to carry over is **how sensor state reaches a
reporter block**. A naive extension would round-trip to the device on every
`acceleration (x)` evaluation; inside a `forever` loop that saturates the link.
The "More" design instead **streams state into a local mirror** and answers
reporters from RAM. This section separates cleanly into what came from the **MIT
extension** (attributed, quotable) and what is a **clean-room concept** observed
from the unlicensed firmware (§6b) — the boundary is marked inline.

### 3.1 The three-transport split *(concept — clean-room, firmware §6b; mirrored in the MIT client)*
The firmware exposes **one custom GATT service** carrying **nine
characteristics**, and — this is the load-bearing design choice — it uses
**three different transport patterns** deliberately, matched to how each kind of
data changes:

1. **Polled reads for bulk continuous state.** High-rate scalars (all digital
   pin levels, buttons, light, temperature, sound; and separately the whole
   motion block: accel, pitch/roll, compass, magnetometer) sit in **read-only
   characteristic buffers that a background loop refills in place** (~19 ms on
   the device). The host **reads** them when it wants a frame. Crucially a
   periodic *notify* path exists in the firmware but is **left disabled** — the
   authors chose client-pull over server-push for continuous state to avoid
   flooding BLE. *(clean-room observation)*
2. **Notifications for discrete asynchronous events.** Button/gesture events,
   pin edge/pulse events, and labelled messages arrive as **notify**
   characteristics — pushed the instant they happen, never polled.
3. **Authorized read-on-demand for expensive samples.** Each analog input pin
   (P0/P1/P2) has **its own characteristic**; a read triggers a fresh,
   pull-resistor-removed, **median-filtered** sample of exactly that pin, then
   restores pull mode. Too costly to keep hot, so it is pulled only when asked.

The lesson: **batch by update-cost and cadence, not arbitrarily.** Continuous +
cheap → one polled frame; discrete → push; expensive → lazy pull.

### 3.2 The characteristic layout *(identifiers — MIT client, attributed)*
The MIT client (`microbit-more.js`) names the service and characteristics with a
128-bit base UUID `…-607f-4151-9091-7d008d6ffc5c` and a 16-bit slot per
characteristic:

| MIT client const | UUID (16-bit slot) | dir | carries |
|---|---|---|---|
| service `ID` | `0b50f3e4-…` | — | the MicroBit-More service |
| `COMMAND_CH` | `0b500100-…` | W + **R once** | all commands out; **read once on connect for the version handshake** (§3.4) |
| `STATE_CH` | `0b500101-…` | R (polled) | 7 B: pin/button digital bitfield, light, temp, sound |
| `MOTION_CH` | `0b500102-…` | R (polled) | 18 B: pitch, roll, accel xyz, heading, mag xyz |
| `PIN_EVENT_CH` | `0b500110-…` | Notify | pin edge/pulse events |
| `ACTION_EVENT_CH` | `0b500111-…` | Notify | button + gesture events |
| `ANALOG_IN_CH[0..2]` | `0b500120/121/122-…` | R on demand | uint16 analog per pin |
| `MESSAGE_CH` | `0b500130-…` | Notify | labelled number/text (**v2 only**) |

The MIT client runs an **updater loop** (100 ms on v1, 50 ms on v2) that reads
`STATE_CH` then `MOTION_CH` each tick into `this._sensors`-style mirrors;
reporters read the mirror. Analog reads are throttled to 100 ms and done on
demand.

### 3.3 Packet formats *(byte layouts — the MIT client decodes these; the encoding is the firmware's, §6b)*
All multi-byte fields **little-endian**.

- **STATE_CH (7 B):** bytes 0–3 = `uint32` digital bitfield — bit *n* is pin
  *n*'s level, with buttons/logo/touch packed into the **high bits (24+index:**
  P0=0,P1=1,P2=2,A=3,B=4,LOGO=5); byte 4 = light (0–255); byte 5 = temperature
  **stored as raw−128** (°C = raw−128, to keep the byte unsigned); byte 6 =
  sound (0–255).
- **MOTION_CH (18 B):** `int16` pitch, `int16` roll (both milli-radians →
  ×180/π/1000 for degrees), accel x/y/z (`int16`, unit G=1024 → milli-g),
  `uint16` heading, mag x/y/z (`int16`, µT).
- **ANALOG_IN_CH[pin] (2 B):** `uint16` 0–1024.
- **Notification frames are a fixed 20 bytes with the *format tag at byte 19***
  — the same characteristic can carry several payload types, discriminated by
  the last byte: `PIN_EVENT=0x11`, `ACTION_EVENT=0x12`, `DATA_NUMBER=0x13`,
  `DATA_TEXT=0x14`. An **action event** leads with a type byte (button=0x01 /
  gesture=0x02); a **button** record is `uint16` componentID + `uint8` eventID +
  `uint32` timestamp; a **gesture** record is `uint8` gestureID + `uint32`
  timestamp. A **pin event** is pin index + event id + `uint32` value (edge
  timestamp or pulse width). A **data-number** is 8-byte label + `float32`;
  **data-text** is 8-byte label + 12 bytes UTF-8.

The **timestamp is used as a sequence/de-dup key** — there is no protocol-level
queue; each event overwrites the shared buffer and fires one notification, and
the host distinguishes a *new* event from a repeat by its timestamp. *(clean-room
observation; the MIT client relies on it.)*

### 3.4 Command encoding *(shape — clean-room §6b; the MIT client emits it, attributed)*
Every command is **one opcode byte + fixed-offset payload**, and the opcode is
**packed: top 3 bits = category, low 5 bits = sub-command** (`id =
(category<<5) | sub`). Categories: CONFIG=0, PIN=1, DISPLAY=2, AUDIO=3, DATA=4.
Representative payloads: PIN set-output `[pin, level]`, set-PWM `[pin, uint16]`,
set-servo `[pin, uint16 angle, uint16 range, uint16 center]`, set-pull, arm pin
event; DISPLAY scroll-text `[delay, …text]` and **two pixel writes** (rows 0–2,
then rows 3–4 — the 25-byte brightness frame is split because it exceeds the
~20-byte characteristic, and the *second* write triggers the paint); AUDIO
play-tone `[uint32 period-µs = 1e6/Hz, uint8 volume]`; CONFIG enable-mic /
enable-touch-on-pin; DATA labelled message. The **packed opcode**, the
**split-LED-frame**, and **tone-as-period-µs** are the distinctive shapes — all
flagged as clean-room in §6b.

### 3.5 Event/hat evaluation on the Scratch side *(MIT client, attributed)*
Notify payloads land in per-kind mirrors (`buttonEvents`, `gestureEvents`,
`_pinEvents`, `receivedData`) tagged with the device timestamp. **Hats are
edge-detected in JS**: a hat compares the latest stored timestamp to a `prev*`
snapshot and a one-shot `setTimeout(…, runtime.currentStepTime)` refreshes the
snapshot once per step, so a hat fires exactly once per new event. Boolean
blocks (`button pressed?`, `pin is high?`) read the *polled* state mirror
instead. **Our design replaces this hand-rolled edge logic with scratch-vm's
`isEdgeActivated`** (§5.2) — a cleaner match to our existing conventions.

---

## 4. Web Bluetooth vs Scratch Link

The stock extension talks to the board through **Scratch Link** — a native
helper app the user installs, which owns the OS Bluetooth stack; the extension
speaks to it over a WebSocket (`io/ble`). That is a hard dependency and an
install step.

`mbit-more-v2` connects **directly from the browser**. Its `scan()` selects a
`WebBLE` peripheral (`ble-web.js`) that uses **`navigator.bluetooth.requestDevice`**
→ `device.gatt.connect()` → `getPrimaryService` / `getCharacteristic`, with
`readValue` / `writeValueWithoutResponse` / `startNotifications`. Device filter:
`namePrefix: 'BBC micro:bit'` + `services:[service UUID]`. **No native helper.**
There is even a **Web Serial** fallback (hold Shift during scan; `navigator.serial`,
VID `0x0d28`/PID `0x0204`, 115200 baud) that reimplements the *same*
characteristic model over a framed byte protocol (start delimiter `0xFF`, a
read/write/notify type byte, a 16-bit channel code = the low hex of each UUID, a
length, the value, a checksum). So **the same `read/write/startNotifications`
API is presented over two transports**, and the block layer is transport-blind.

**What this implies for a browser-first tool like ours.** BrickWright-lite is a
browser app (and a Tauri wrap of it); a **no-native-helper** path is exactly
right. Web Bluetooth is available in Chromium-family browsers and the Tauri
webview; Web Serial gives a fallback and also matches the WebUSB/`dapjs` path we
already ship for flashing (`MICROBIT-NATIVE.md §1`). **But** — and this is the
BrickWright-specific fork in the road — our v1 target is **not** a physically
connected board over BLE. It is the **in-browser MicroPython simulator** we
already host (`static/microbit-sim/`, `MICROBIT-NATIVE.md §1`). So our extension
has **two backends behind one block set** (§5.4): the simulator backend needs no
Bluetooth at all, and the live-BLE backend (a later stage) uses the Web
Bluetooth model above. Web Bluetooth is the *right connection model when we
connect to hardware*; it is not on the v1 critical path.

---

## 5. Design proposal — the BrickWright `microbitPlus` extension

Our extension is **not a fork of mbit-more**. It is our own block set, built on
**our** conventions, covering the "More" envelope and closing two of its gaps
(radio group/power; an honest I²C/SPI story). It plugs into the gallery the way
every BrickWright extension does.

### 5.0 Registration & housekeeping (mechanics)
A new BrickWright extension is **three coordinated edits, all in `overlay/`**
(the `packages/` tree is upstream vendor; BrickWright code lives in `overlay/`):

1. **Gallery card** — `overlay/scratch-gui/src/lib/libraries/extensions/index.jsx`:
   an entry `{ name: 'micro:bit+', extensionId: 'microbitPlus', iconURL, insetIconURL,
   description, featured, connectionIconURL… }`. (Several cards may share one
   `extensionId`, as the single `stc12` extension surfaces five device cards.)
2. **VM builtin registry** —
   `overlay/scratch-vm/src/extension-support/extension-manager.js`,
   `builtinExtensions`: `microbitPlus: () => require('../extensions/crispstrobe/microbitPlus')`.
3. **The extension file** — `overlay/scratch-vm/src/extensions/crispstrobe/microbitPlus/index.js`,
   written in the **TurboWarp/Xcratch style wrapped by `adapter.js`**
   (`makeCrispExtension`): a `class MicrobitPlus { getInfo(){…} …opcode methods }`
   registered with `Scratch.extensions.register(new MicrobitPlus(runtime))`. This
   is the crispstrobe path (`stc12`, `devices`, `circuit` all use it), **not** the
   stock `scratch3_<name>` path.

### 5.1 The display block — reuse the stock 5×5 matrix field for v1
**Refinement to `MICROBIT-NATIVE.md §1a`:** the simplest v1 display block reuses
Scratch's stock **`Blockly.FieldMatrix`** (`packages/scratch-blocks/core/field_matrix.js`),
which is **already exactly 5×5** — value a **25-char row-major binary string**
(`'0'`/`'1'`), declared as an argument of `type: ArgumentType.MATRIX` (the VM
auto-inserts the `<shadow type="matrix">` carrying a `FieldMatrix` named
`MATRIX`). So the micro:bit v1 display needs **no new field** — the single
simplest part of this design.

Note on `FieldLed8x8`: it **does exist**, on the unmerged `feat/a2-matrix-editor`
branch — the 8×8 brightness painter built for the A2 matrix (a runtime-registered
Blockly field cycling 4 levels per cell). This survey read `main`, where it is
not yet present, hence the earlier "no such class" reading. It is 8×8, not the
micro:bit's 5×5, so it is not the v1 field here — but it is the right MODEL for
the "later" brightness variant: **micro:bit natively supports per-pixel
brightness (0–9)**, so a 5×5, 0–9-level field modelled on `FieldLed8x8` is the
natural brightness display (§5.5), whereas stock `FieldMatrix` is on/off only.
v1 ships on/off (maps to full brightness); the brightness field is the upgrade.

### 5.2 Event hats — our edge model, not mbit-more's hand-rolled one
Every hardware hat is a `BlockType.HAT` with **`isEdgeActivated: true`**; the
opcode method returns the **current polarity-aware boolean level**, and
scratch-vm remembers the prior value and fires the stack only on a **false→true
transition**. This is the exact `whenpin` pattern from
`overlay/scratch-vm/src/extensions/crispstrobe/stc12/index.js` — the same one the
A2 matrix keypad uses (keypad keys are MK-pins read through a generic `whenpin`).
We do **not** reproduce mbit-more's `setTimeout`/`prev*` snapshot logic; the VM
already does edge detection correctly and it is our house style.

### 5.3 The v1 block set
Opcode names follow the crispstrobe lowercase-concatenated convention
(cf. `whenpin`). Shape: **C**ommand / **R**eporter / **B**oolean / **H**at.
Menu names in braces.

**Display & LEDs**
| opcode | text | shape | args |
|---|---|---|---|
| `showmatrix` | show pattern [MATRIX] | C | MATRIX (ArgumentType.MATRIX, 5×5) |
| `showtext` | show text [TEXT] | C | TEXT string |
| `scrolltext` | scroll text [TEXT] delay [MS] ms | C | TEXT string, MS number=120 |
| `cleardisplay` | clear display | C | — |
| `plot` | plot x [X] y [Y] [ON/OFF] | C | X 0–4, Y 0–4, STATE menu |

**Buttons, logo, gestures (events)**
| opcode | text | shape | args |
|---|---|---|---|
| `whenbutton` | when button [A/B/any] [pressed/released] | H (edge) | BTN menu, EVENT menu |
| `isbutton` | button [A/B/any] pressed? | B | BTN menu |
| `whenlogo` | when logo [touched/released] | H (edge) | EVENT menu |
| `whengesture` | when [gesture] | H (edge) | GESTURE menu (11) |
| `isgesture` | [gesture] happening? | B | GESTURE menu |

**Motion / orientation (reporters)**
| opcode | text | shape | args |
|---|---|---|---|
| `accel` | acceleration [x/y/z/strength] | R | AXIS menu → milli-g |
| `pitch` | pitch (°) | R | — |
| `roll` | roll (°) | R | — |
| `compass` | compass heading (°) | R | — |
| `magforce` | magnetic force [x/y/z/absolute] | R | AXIS menu → µT |

**Environment (reporters)**
| opcode | text | shape | args |
|---|---|---|---|
| `light` | light level | R | 0–255 |
| `temp` | temperature (°C) | R | — |
| `sound` | sound level | R | 0–255 (v2; lazily enables mic) |

**Pins / GPIO**
| opcode | text | shape | args |
|---|---|---|---|
| `digitalwrite` | set pin [PIN] digital [0/1] | C | PIN menu {gpio}, LEVEL menu |
| `digitalread` | pin [PIN] digital value | R | PIN menu |
| `ispinhigh` | pin [PIN] is high? | B | PIN menu |
| `analogread` | analog value of pin [P0/P1/P2] | R | PIN menu {analogIn} → 0–1023 |
| `analogwrite` | set pin [PIN] analog [0–100] % | C | PIN menu, PCT number |
| `setpull` | set pin [PIN] pull [none/up/down] | C | PIN menu, MODE menu |
| `whentouch` | when pin [P0/P1/P2] touched | H (edge) | PIN menu |
| `istouch` | pin [P0/P1/P2] touched? | B | PIN menu |

**Actuators**
| opcode | text | shape | args |
|---|---|---|---|
| `playtone` | play tone [FREQ] Hz for [MS] ms | C | FREQ number=440, MS number |
| `playnote` | play note [NOTE] | C | NOTE menu (C4…B5) |
| `stoptone` | stop tone | C | — |
| `servo` | set pin [PIN] servo angle [DEG] | C | PIN menu, DEG 0–180 |
| `servocont` | set pin [PIN] continuous servo [SPD] % | C | PIN menu, SPD −100…100 |

**Radio — this is where we go past mbit-more.** mbit-more has only a labelled
BLE-relayed message channel and models neither group nor power. The real
micro:bit `radio` module has both, and it is the natural way two boards talk
without a host. We expose the actual radio:
| opcode | text | shape | args |
|---|---|---|---|
| `radioon` | turn radio on group [G] power [P] | C | G 0–255, P 0–7 |
| `radiosendnum` | radio send number [N] | C | N number |
| `radiosendstr` | radio send text [S] | C | S string |
| `radiosendkv` | radio send [KEY] = [VALUE] | C | KEY string, VALUE number |
| `whenradionum` | when radio receives a number | H | — |
| `radiolastnum` | last radio number | R | — |
| `whenradiostr` | when radio receives text | H | — |
| `radiolaststr` | last radio text | R | — |

**Connection**
| opcode | text | shape | args |
|---|---|---|---|
| `whenconn` | when micro:bit [connected/disconnected] | H | STATE menu |

Menus: `gpio` = {0,1,2,8,12,13,14,15,16}; `analogIn` = {0,1,2}; `gesture` =
shake, tilt up/down/left/right, face up/down, freefall, 3g/6g/8g; `AXIS` =
x/y/z/(strength|absolute); `BTN` = A/B/any; pull = none/up/down.

**v1 count:** ~40 blocks — comfortably past both stock (9) and the mbit-more
envelope (~30), with radio group/power added.

### 5.4 Two backends behind one block set (the BrickWright-specific design)
This is the crux and the tie-in to `MICROBIT-NATIVE.md §5 Stage 1`. The block
set above is a **façade**; behind it are two interchangeable backends:

- **Backend S — the simulator (v1 target).** Blocks drive the **in-browser
  MicroPython WASM sim** we already host. No Bluetooth. A command block like
  `digitalwrite(P0,1)` executes by feeding the sim the corresponding MicroPython
  (`pin0.write_digital(1)`); reporters read sim state over the existing
  postMessage bridge. This reuses the exact vocabulary our
  `sb3-creator-micropython` reader already round-trips: `pin0.write_digital`,
  `pin0.read_analog`, `pin0.write_analog`, `music.pitch`, `button_a.is_pressed`,
  the `@bw device`/`@bw pin` markers, the `_level` toggle dict, the parking
  lines. So **the emitter side is mostly already specified** — the reader tells
  us precisely what the writer must produce.
- **Backend B — live BLE (later).** The same blocks talk to a connected board
  over **Web Bluetooth** (§4), mirroring streamed sensor state locally (§3) and
  emitting the packed-opcode commands. Needs a companion firmware that speaks
  our service — re-derived clean-room (§6b) or built on the MIT MicroPython radio
  path; a Stage-4 decision.

The façade means a `getInfo()` opcode method dispatches to whichever backend is
attached (`this._backend.digitalWrite(pin,level)`), so the block set, menus, and
edge-hat semantics are written **once**.

### 5.5 Dual lowering — the same dialect program → blocks AND MicroPython
`MICROBIT-NATIVE.md §5 Stage 1` requires that the same BrickWright dialect
program lower to **both** these gallery blocks and the MicroPython the
simulator/hardware runs. The mapping is direct because our dialect vocabulary,
these blocks, and the MicroPython emitter are three views of the same operations:

| dialect (BrickWright) | `microbitPlus` block | MicroPython (sim/hw) |
|---|---|---|
| `turn on led` (P0 active-low) | `digitalwrite P0 0` | `pin0.write_digital(0) # led off` |
| `set pot` … `read pot` | `analogread P1` | `pin1.read_analog()` |
| `set buzzer to 440 hz` | `playtone 440` | `music.pitch(440, pin=pin0)` |
| `read button_a` | `isbutton A` | `button_a.is_pressed()` |
| `WHEN key MK pressed` | `whentouch`/`whenbutton` | edge poll in `bw_script()` |
| display grid | `showmatrix` | `display.show(Image(...))` |

Two consequences: (a) the **`@bw` line markers** the debugger needs
(`MICROBIT-NATIVE.md §3`) are emitted by the same MicroPython writer that backs
Backend S, so blocks-run and sim-run share one source of truth; (b) a program
authored as **blocks** and a program authored as **dialect** converge on the same
MicroPython, which is the acceptance oracle in `MICROBIT-NATIVE.md §6.5` ("same
observable behaviour on both targets").

### 5.6 v1 vs later
**v1 (ship against the simulator, Backend S):**
- Display: `showmatrix`, `showtext`, `scrolltext`, `cleardisplay`, `plot`.
- Buttons/logo/gesture hats + booleans (`isEdgeActivated`).
- Sensor reporters: accel/pitch/roll/compass/magforce/light/temp/sound.
- Pins: digital read/write, analog read, analog write (PWM), pull, touch.
- Actuators: tone/note/stop, servo (angle + continuous).
- Radio: on/group/power, send num/str/kv, receive hats + last-value reporters.
- Connection-state hat (reports sim-attached).

**Later (needs a fork or a custom field or hardware):**
- **Live BLE backend (Backend B)** over Web Bluetooth + a companion firmware
  (clean-room service or MIT MicroPython radio path) — the actual "connect a
  board" story.
- **Per-pixel brightness** display (custom field beyond `FieldMatrix`).
- **Pin edge/pulse event blocks** (`listen … event`, `when catch … at pin`,
  pulse-width reporter) — richer than v1 touch, matches mbit-more §2.1.
- **I²C / SPI expert blocks** — mbit-more has none; expose only where the sim's
  MicroPython (`i2c`, `spi`) or hardware supports it, guarded as expert.
- **Serial/UART** block surface (mbit-more treats serial as transport only).
- **Sound v2 extras** (`microphone` events, `set volume`), **speaker on/off**.

---

## 6. Provenance — the licence-hygiene record

Two explicit lists, per `MICROBIT-NATIVE.md §0` and the ledcube §7 discipline.
This is the record that governs what may be *adapted with attribution* vs what
must be *re-implemented from our own understanding*.

### 6a. Derived from the MIT `mbit-more-v2` — attributed, safe to adapt
Attribution: **micro:bit More extension © 2020–2022 Koji Yokokawa, MIT
(`github.com/microbit-more/mbit-more-v2`).** These are extension-side facts we
may reuse and even quote:

- The **feature/block envelope** (§2) — which capabilities a "beyond legacy"
  micro:bit extension should expose, and their block shapes (command/reporter/
  boolean/hat) and argument menus. Our block set (§5.3) is our own redesign of
  this envelope, not a paste.
- The **client-side streaming architecture** (§3.2, §3.5): an updater loop that
  polls STATE/MOTION into a local sensor mirror, reporters answering from the
  mirror, analog read throttled on demand, notify payloads landing in per-kind
  event mirrors.
- The **specific UUIDs and byte offsets** the client uses (§3.2, §3.3). These are
  **interface facts**: reuse them **only** if we deliberately want
  wire-compatibility with existing micro:bit-More firmware/hosts, and if so
  document it as *interoperability*, not authorship. For an independent service
  we choose our own (see 6b caveat).
- The **Web Bluetooth connection method** (§4): `navigator.bluetooth.requestDevice`
  with a name-prefix + service filter, `gatt.connect`, per-characteristic
  read/write/startNotifications; and the Web Serial fallback shape. Directly
  adaptable.
- Client-side **quirk handling** we should copy to interop correctly: temperature
  as raw−128, tone as period-µs, PWM range 0–1024, percent scaling, the 20-byte
  notify frame with the **format tag at byte 19**.

### 6b. Clean-room concepts observed from the unlicensed `pxt-mbit-more-v2` — re-derive, do not copy
Source has **NO licence (all rights reserved)**;
`github.com/microbit-more/pxt-mbit-more-v2`. **No code was or may be copied.**
The items below are *behaviours and shapes* we understand and must **re-implement
from our own design** if we ever build Backend B's companion firmware. Each is
recorded so a future implementer re-derives it rather than transcribing it:

1. The **packed opcode framing** — 3-bit category / 5-bit sub-command in the
   first command byte (§3.4). Re-choose our own framing.
2. The **three-transport split** (§3.1): polled reads for bulk continuous state,
   notify for discrete events, authorized read-on-demand per analog pin — **and**
   the deliberate choice to leave continuous-state notification *disabled*.
3. **Version handshake by re-reading the command characteristic** after connect
   (hardware code + protocol code in the same buffer commands are written to).
4. The **exact packing** of the STATE bitfield (per-pin bit positions +
   buttons/logo/touch in high bits) and the 18-byte MOTION frame (offsets,
   radians×1000, µT scaling, axis-sign/heading normalisation).
5. **Splitting the 5×5 LED frame across two writes** (rows 0–2, then 3–4) because
   a full brightness frame exceeds the ~20-byte characteristic; the second write
   triggers the paint.
6. Folding **button + gesture into one tagged action-event record**, and routing
   **pin-touch through the button event path** into both the digital bitfield and
   the event stream.
7. **Timestamp-as-sequence-number** in event records (no protocol-level queue;
   dedup by timestamp).
8. **Median-of-N, pull-save/restore analog read** inside the read hook;
   running-mean light smoothing; clamped/scaled mic.
9. The **labelled-message mailbox** (fixed label field + typed content, host↔
   device) and the optional **serial transport that re-tags characteristic
   payloads** with their 16-bit id.

**Obvious hardware/CODAL consequences** (observed, but *not* novel and not
anyone's IP): the set of sensors and their SI units; tone = PWM on the speaker
pin; the GPIO allow-list; CODAL generating the raw button/gesture/pin-edge
events; little-endian fields; offset-encoding temperature to stay unsigned.

**Caveat carried from the firmware study:** if Backend B's goal is an
*independent* protocol, we pick our own UUIDs and byte layouts (6b items are then
purely conceptual guidance). If the goal is to interoperate with existing
micro:bit-More firmware/hosts, the specific UUIDs/offsets in **6a** are interface
values we would match — documented as interoperability. The two goals must not be
blurred: matching-for-interop is defensible; copying-the-firmware is not, and the
firmware is off-limits regardless.

---

## 7. Bottom line

- **Gap over stock:** 9 blocks → ~40; a single fixed 10-byte packet → a
  streaming protocol (3 polled/notify/on-demand transports). Categorically
  beyond legacy, not a polish pass.
- **The one idea to carry:** **stream sensor state into a local mirror and answer
  reporters from RAM** — batched by cadence (polled bulk / pushed events / lazy
  analog).
- **Our conventions win us three simplifications:** the display grid is the stock
  5×5 `FieldMatrix` (no new field), hats use `isEdgeActivated` (no hand-rolled
  edge logic), and registration is the standard crispstrobe three-edit path.
- **The BrickWright-specific design** is **two backends behind one block set**:
  ship v1 against the MicroPython **simulator** (no Bluetooth), add a **live BLE**
  backend later; both driven by one façade, and both fed by the same dialect →
  blocks / dialect → MicroPython lowering that `MICROBIT-NATIVE.md §5` mandates.
- **Provenance is clean:** the block envelope, client streaming architecture, and
  Web Bluetooth method come from the **MIT** extension (attributed, §6a); the
  firmware protocol shapes are **clean-room concepts** to be re-derived (§6b),
  with UUIDs treated as interop facts, never authorship.
