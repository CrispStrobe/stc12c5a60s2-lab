# stc12c5a60s2-lab

🇬🇧 English · [🇩🇪 Deutsch](README.de.md)

Bare-metal experiments on the **STC12C5A60S2**, driven entirely from macOS
with open-source tools — no Windows, no STC-ISP.exe, no dedicated programmer.

This first example blinks two LEDs. Everything else in the repo exists to make
that first blink reproducible from a bag of parts.

The longer-term goal is a **[BrickWright](https://github.com/CrispStrobe/brickwright)
extension** that targets this chip: Scratch blocks → C → `.hex`, the same way
BrickWright already transpiles to LEGO NXT/EV3 bytecode. See
[docs/ROADMAP.md](docs/ROADMAP.md).

---

## Table of contents

1. [Pin layout](#1-pin-layout)
2. [Connecting to the IC under macOS](#2-connecting-to-the-ic-under-macos)
3. [Wiring](#3-wiring)
4. [The script](#4-the-script)
5. [Build and flash](#5-build-and-flash)
6. [Troubleshooting](#6-troubleshooting)
7. [Repo layout](#7-repo-layout)
8. [Where this is going](#8-where-this-is-going)

---

## 1. Pin layout

The STC12C5A60S2 is a 1T (single-clock-per-machine-cycle) 8051 core — roughly
8–12× faster than a classic 12T 8051 at the same clock. 60 KB flash, 1280 B RAM
(256 B internal + 1024 B auxiliary), 1 KB EEPROM, 2 UARTs, 8-channel 10-bit ADC,
SPI, PCA/PWM.

It ships in PDIP-40, PLCC-44, LQFP-44, LQFP-48 and QFN-40. **This repo assumes
PDIP-40**, the through-hole part you can put in a breadboard.

### PDIP-40 pinout

```
                      ┌─────────∪──────────┐
  CLKOUT2/ADC0/P1.0 ──│  1              40 │── VCC
          ADC1/P1.1 ──│  2              39 │── P0.0/AD0
 RxD2/ECI/ADC2/P1.2 ──│  3              38 │── P0.1/AD1
TxD2/CCP0/ADC3/P1.3 ──│  4              37 │── P0.2/AD2
  SS/CCP1/ADC4/P1.4 ──│  5              36 │── P0.3/AD3
     MOSI/ADC5/P1.5 ──│  6              35 │── P0.4/AD4
     MISO/ADC6/P1.6 ──│  7              34 │── P0.5/AD5
     SCLK/ADC7/P1.7 ──│  8              33 │── P0.6/AD6
           RST/P4.7 ──│  9    STC12     32 │── P0.7/AD7
       INT/RxD/P3.0 ──│ 10   C5A60S2    31 │── EX_LVD/RST2/P4.6
           TxD/P3.1 ──│ 11              30 │── ALE/P4.5
          INT0/P3.2 ──│ 12              29 │── NA/P4.4
          INT1/P3.3 ──│ 13              28 │── P2.7/A15
CLKOUT0/INT/T0/P3.4 ──│ 14              27 │── P2.6/A14
CLKOUT1/INT/T1/P3.5 ──│ 15              26 │── P2.5/A13
            WR/P3.6 ──│ 16              25 │── P2.4/A12
            RD/P3.7 ──│ 17              24 │── P2.3/A11
              XTAL2 ──│ 18              23 │── P2.2/A10
              XTAL1 ──│ 19              22 │── P2.1/A9
                GND ──│ 20              21 │── P2.0/A8
                      └────────────────────┘
```

Pin 1 is at the notched end. The `INT`/`WR`/`RD` names are active-low in the
datasheet (drawn with an overbar); plain ASCII here.

### Pin table

| Pin | Name | What it is |
|----:|------|------------|
| 1–8 | **P1.0 – P1.7** | GPIO. Also the 8 ADC inputs (ADC0–ADC7), UART2 (P1.2/P1.3), SPI (P1.4–P1.7), PCA/CCP (P1.3/P1.4), and CLKOUT2 (P1.0). |
| 9 | **RST / P4.7** | Reset. Active **high** — hold high for ≥2 machine cycles to reset. Defaults to reset function; becomes GPIO P4.7 only if you reconfigure it in the ISP options (and then you must use an external crystal). |
| 10 | **P3.0 / RxD** | UART1 receive — **this is the ISP download pin**. |
| 11 | **P3.1 / TxD** | UART1 transmit — **this is the ISP upload pin**. |
| 12 | **P3.2 / INT0** | GPIO / external interrupt 0. |
| 13 | **P3.3 / INT1** | GPIO / external interrupt 1. |
| 14 | **P3.4 / T0 / CLKOUT0** | GPIO / Timer 0 input / programmable clock out. |
| 15 | **P3.5 / T1 / CLKOUT1** | GPIO / Timer 1 input / programmable clock out. |
| 16 | **P3.6 / WR** | GPIO / external-memory write strobe. |
| 17 | **P3.7 / RD** | GPIO / external-memory read strobe. |
| 18 | **XTAL2** | Crystal out. Leave **floating** if you drive XTAL1 from an external oscillator. |
| 19 | **XTAL1** | Crystal in. |
| 20 | **GND** | Ground. |
| 21–28 | **P2.0 – P2.7** | GPIO. Doubles as the high address byte A8–A15 when using external memory. |
| 29 | **NA / P4.4** | GPIO **only after** setting `P4SW.4`. Otherwise a weak pull-up with no function. |
| 30 | **ALE / P4.5** | Address Latch Enable. GPIO only after setting `P4SW.5`. |
| 31 | **EX_LVD / RST2 / P4.6** | Low-voltage detect / secondary reset. GPIO only after setting `P4SW.6`. |
| 32–39 | **P0.7 – P0.0** | GPIO (note the **descending** order: pin 32 is P0.7, pin 39 is P0.0). Doubles as AD0–AD7. |
| 40 | **VCC** | Supply. **3.5 – 5.5 V** for the `STC12C…` part. |

> [!IMPORTANT]
> **There is no `EA` pin.** If you are coming from an AT89C51/52, you are used
> to tying `EA` high so the CPU runs from internal flash. STC removed it — the
> STC12 always runs from internal flash. Pin 31 is `EX_LVD/RST2/P4.6` instead.

> [!WARNING]
> **`STC12C5A60S2` is a 5 V part (3.5–5.5 V).** It will not run reliably at
> 3.3 V. The 3.3 V-capable sibling is `STC12LE5A60S2` (2.1–3.6 V), and *that*
> one will be damaged by 5 V. Check the marking on your chip before applying
> power.

### Only P4.4–P4.7 exist on PDIP-40

The datasheet lists a full 8-bit Port 4, but on the 40-pin package only
**P4.4, P4.5, P4.6 and P4.7** are bonded out. `P4.0–P4.3` need PLCC-44,
LQFP-44 or LQFP-48. Port 5 (P5.0–P5.3) needs LQFP-48.

### I/O port modes

Every pin on every port can be independently put in one of four modes, via a
pair of registers `PxM1` / `PxM0` (one bit per pin):

| `PxM1` | `PxM0` | Mode | Drive |
|:---:|:---:|---|---|
| 0 | 0 | **Quasi-bidirectional** — the power-on default, classic 8051 behaviour | Sinks up to 20 mA; sources only ~230 µA through a weak pull-up |
| 0 | 1 | **Push-pull** (strong pull-up) | Sources *and* sinks up to 20 mA — always add a series resistor |
| 1 | 0 | **Input-only** (high impedance, Schmitt trigger) | — |
| 1 | 1 | **Open-drain** | Sinks only; needs an external pull-up |

Register addresses: `P0M1`=0x93 `P0M0`=0x94, `P1M1`=0x91 `P1M0`=0x92,
`P2M1`=0x95 `P2M0`=0x96, `P3M1`=0xB1 `P3M0`=0xB2, `P4M1`=0xB3 `P4M0`=0xB4.

> [!NOTE]
> **This asymmetry is the single most common beginner trap.** Out of reset a
> pin can *sink* 20 mA but can only *source* about a quarter of a milliamp.
> That is why the LEDs in this repo are wired **active-low** (see §3) — it is
> the arrangement that works in every mode. §4.1 says "the whole chip had
> better drive lower than **120 mA**". Per-port ~80 mA is 8051 family guidance
> (not in this datasheet). Budget ~10 mA per LED, or multiplex.

### Speed gotcha inherited from being 1T

On a classic 8051 an I/O access takes 12 clocks, which conveniently gave
external hardware time to settle. On the STC12 it takes **4**. If you drive a
pin and then immediately read back an external signal, insert one or two `nop`s
— the instruction has completed but the outside world has not caught up.

---

## 2. Connecting to the IC under macOS

There is **no JTAG and no SWD** on this chip, and you do not need a dedicated
programmer. The STC12 has a mask-ROM bootloader ("ISP monitor") that speaks a
serial protocol on `P3.0/P3.1`. So the entire link is:

```
   Mac ──USB──▶ USB-to-TTL adapter ──3 wires──▶ STC12C5A60S2
```

### 2.1 The adapter

Any 5 V-capable USB-TTL serial adapter works. The common three:

| Chip | macOS driver | Shows up as |
|---|---|---|
| **CH340 / CH341** | Built into macOS 11 Big Sur and later | `/dev/cu.wchusbserial*` |
| **CP2102 / CP210x** | Built into macOS 11 and later | `/dev/cu.usbserial-*` or `/dev/cu.SLAB_USBtoUART` |
| **FT232R** | Built into macOS (AppleUSBFTDI) | `/dev/cu.usbserial-*` |
| **An Arduino Uno** | whatever your Uno already uses | `/dev/cu.usbmodem*` |

> [!TIP]
> **No adapter? An Arduino Uno is one.** Its ATmega16U2 is a USB-serial bridge.
> Jumper `RESET` to `GND` to hold the ATmega328P out of the way, then treat the
> board's `RX`/`TX` header pins as the adapter — but note these are wired
> *straight through*, so Uno `TX`(1) → MCU pin 11 and Uno `RX`(0) → MCU pin 10,
> the opposite of the crossover in §3.3. It runs at 5 V, which is what we want.

> [!CAUTION]
> **"ISP" here does not mean SPI.** Coming from AVR, `ISP` means a
> MOSI/MISO/SCK programmer like a USBasp or mySmartUSB. The STC12 has **no such
> interface** — those pins exist (P1.5–P1.7) but they are ordinary SPI
> peripherals, not a programming port. The only way in is the serial bootloader
> on P3.0/P3.1. This is the single most common wrong assumption about this chip.

On a current macOS you should not need to install anything. If your adapter
does not enumerate, install the vendor driver
([WCH](https://www.wch-ic.com/downloads/CH341SER_MAC_ZIP.html),
[Silicon Labs](https://www.silabs.com/developer-tools/usb-to-uart-bridge-vcp-drivers),
[FTDI](https://ftdichip.com/drivers/vcp-drivers/)) and approve it in
**System Settings → Privacy & Security**.

> [!IMPORTANT]
> **Set the adapter's voltage jumper to 5 V**, and use the `/dev/cu.*` node,
> never `/dev/tty.*`. On macOS the `tty.*` node blocks on carrier-detect and
> your flash will just hang. The `cu.*` ("call-up") node does not.

Check what you have:

```bash
make ports
```

### 2.2 The toolchain

Three pieces: a compiler, a hex packer, and the ISP flasher.

```bash
# compiler + packihx + makebin  (SDCC targets the mcs51/8051 family)
brew install sdcc

# stcgal — the open-source replacement for STC's Windows-only STC-ISP.exe
brew install pipx && pipx ensurepath
pipx install stcgal
```

Or just run the helper:

```bash
./tools/setup-macos.sh
```

Verify:

```bash
sdcc --version     # 4.x, must list "mcs51" among the targets
stcgal --version   # 1.10 or newer
```

> **Why `pipx` and not `pip3 install`?** Homebrew's Python refuses global
> `pip install` (PEP 668). `pipx` gives `stcgal` its own venv and still puts
> the binary on your `PATH`.

`stcgal` is MIT-licensed. SDCC is GPL, and its bundled
`mcs51/stc12.h` header — which is where all our `P1M0`, `P4SW`, `AUXR`
definitions come from — is GPL-2+ **with a linking exception**, so the
binaries you build from it are yours.

### 2.3 How ISP entry actually works

This is the part that trips everyone up:

> **The STC bootloader only listens during the first few milliseconds after a
> cold power-on.** Pressing a reset button is *not* enough. You must actually
> remove and reapply VCC while `stcgal` is already running and waiting.

So the flashing ritual is:

1. Run `make flash`. It prints `Waiting for MCU, please cycle power:`.
2. *Then* power-cycle the chip.
3. `stcgal` catches the handshake at 2400 baud, negotiates up to 115200, and
   writes the flash.

If you want to automate step 2, wire the adapter's **DTR** line to a
transistor that switches the MCU's VCC, and add `-a` to the stcgal call — see
[rgm3/ledcube444](https://github.com/rgm3/ledcube444) for a photo of exactly
that hack. Until then, a small switch or just pulling the VCC jumper is fine.

---

## 3. Wiring

### 3.1 Bill of materials

| Qty | Part |
|---:|---|
| 1 | STC12C5A60S2, PDIP-40 |
| 1 | 40-pin DIP socket (optional but kind to the chip) |
| 1 | USB-TTL adapter, 5 V |
| 2 | LEDs (any colour) |
| 2 | 1 kΩ resistors — current limit for the LEDs |
| 1 | 1 kΩ resistor — reset pull-down |
| 1 | 100 nF ceramic capacitor (marked `104`) — decoupling |
| 1 | 10 µF electrolytic capacitor — bulk decoupling |
| — | breadboard + jumper wires |
| *opt.* | 11.0592 MHz crystal + 2× 22–47 pF capacitors |

### 3.2 The minimum system

The STC12 needs almost nothing to run:

```
   +5V ─────┬──────────┬────────────────────── pin 40   VCC
            │          │
          ──┴──      ──┴──
          10 µF      100 nF     ← put both as close to pins 40/20
          ──┬──      ──┬──         as you can physically get them
            │          │
   GND ─────┴──────────┴───┬────────────────── pin 20   GND
                           │
                           └──[ 1 kΩ ]──────── pin 9    RST
```

* **Reset (pin 9):** reset is **active high** here, so it must be held low to
  run. A 1 kΩ resistor to GND is all you need below 12 MHz. (The classic
  10 kΩ + 10 µF RC network is for *active-low* 8051s — don't copy it here.)
* **Clock:** you can leave XTAL1/XTAL2 (pins 19/18) **completely empty**. The
  chip free-runs on its internal RC oscillator. That is fine for blinking.
  * The internal RC is only specified as **11–17 MHz at 5 V**, so timing will
    be approximate — you will need to tune `FOSC` (see §4).
  * For anything needing real timing (UART, PWM), fit an 11.0592 MHz crystal
    between pins 19 and 18 with a 22–47 pF cap from each pin to GND, and set
    `FOSC=11059200`.
* **No `EA` pin to tie high** — see §1.

### 3.3 The programming link

Three wires. **TX and RX cross over.**

```
   USB-TTL adapter              STC12C5A60S2
   ───────────────              ────────────
        TXD  ──────────────────▶  pin 10   P3.0 / RxD
        RXD  ◀──────────────────  pin 11   P3.1 / TxD
        GND  ──────────────────   pin 20   GND
        5V   ──────────────────   pin 40   VCC   (see note)
```

> [!CAUTION]
> **Pick exactly one power source.** Either let the adapter's 5 V feed the
> MCU (fine for two LEDs — total draw is a few mA), *or* use a separate 5 V
> supply. Never both at once. If you use a separate supply, still connect the
> adapter's GND to the MCU's GND, or the serial link has no common reference
> and the handshake will never complete.

If you power from the adapter, the "power cycle" in §2.3 is just unplugging
the 5 V jumper wire for a second — that is the cleanest way to do it.

### 3.4 The LEDs

Wired **active-low**: the MCU sinks the current, VCC supplies it.

```
                                        ┌──────────────┐
   +5V ──[ 1k ]──▶|── LED1 ─────────────┤  1   P1.0    │
                                        │              │
   +5V ──[ 1k ]──▶|── LED2 ─────────────┤  2   P1.1    │
                                        └──────────────┘

   anode ──[ resistor ]── +5V        cathode ── MCU pin
```

Writing **0** to the pin lights the LED. Writing **1** turns it off.

Why this way round? Because of the sink/source asymmetry in §1: a
quasi-bidirectional pin sources ~230 µA (§4.1), which is not enough to light
an LED visibly, but sinks 20 mA happily. §4.6 specifies exactly
this: *"For weak pull-up / quasi-bidirectional I/O, use sink current to drive
the LED, current-limiting resistor greater than 1 kΩ, minimum not less than
470 Ω."*

The code additionally switches P1.0/P1.1 to **push-pull** mode, which does not
change the wiring — it just makes the "off" state a hard VCC instead of a weak
pull-up, so the LEDs go properly dark.

> If you would rather wire the LEDs the other way (pin → resistor → LED → GND,
> "active-high"), that also works *because* the code sets push-pull mode — but
> then you must invert `LED_ON` / `LED_OFF` in `include/board.h`, and the LEDs
> will be lit for the first few ms after every reset, before `board_init()`
> runs.

### 3.5 Checkpoint

Before plugging in USB, check with a multimeter:

- [ ] Pin 40 ↔ pin 20: **not** shorted.
- [ ] Pin 9 (RST) reads ~0 V once powered.
- [ ] Adapter GND and MCU GND are the same net.
- [ ] Adapter TXD goes to pin 10, RXD to pin 11 (crossed, not straight).
- [ ] Every LED has a resistor in series.

Then, with no code flashed yet:

```bash
make info
```

Power-cycle when prompted. If it prints the chip's model, bootloader version
and MCU ID, your wiring and toolchain are both correct — the hard part is done.

---

## 4. The script

[`src/01-blink/main.c`](src/01-blink/main.c) alternates the two LEDs six times,
then flashes both together twice at exactly 1 Hz, forever. The 1 Hz phase is
there so you can hold a watch to it and check your clock assumption.

```c
#include "board.h"
#include "delay.h"

void main(void)
{
    unsigned char i;

    board_init();
    delay_init();

    for (;;) {
        /* Phase 1: alternate, six times, 150 ms per step. */
        for (i = 0; i < 6; i++) {
            LED1 = LED_ON;  LED2 = LED_OFF;  delay_ms(150);
            LED1 = LED_OFF; LED2 = LED_ON;   delay_ms(150);
        }

        /* Phase 2: both together, 1 s on / 1 s off, twice. */
        for (i = 0; i < 2; i++) {
            LED1 = LED_ON;  LED2 = LED_ON;   delay_ms(1000);
            LED1 = LED_OFF; LED2 = LED_OFF;  delay_ms(1000);
        }
    }
}
```

### What the two headers do

**[`include/board.h`](include/board.h)** — all the hardware facts in one place:

```c
#define LED1    P1_0        /* PDIP-40 pin 1 */
#define LED2    P1_1        /* PDIP-40 pin 2 */
#define LED_ON  0           /* active-low: sink the current */
#define LED_OFF 1

static void board_init(void)
{
    P1M1 &= ~0x03;          /* M1 = 0 ┐                        */
    P1M0 |=  0x03;          /* M0 = 1 ┴─ P1.0, P1.1 push-pull  */
    LED1 = LED_OFF;
    LED2 = LED_OFF;
}
```

`P1_0` and `P1M0` come from SDCC's bundled `<stc12.h>`, which `board.h`
includes. There is no vendor SDK to install.

**[`include/delay.h`](include/delay.h)** — a millisecond delay built on
**Timer 0** rather than a busy-wait loop, so its accuracy depends only on
`FOSC_HZ` and not on how SDCC felt like compiling a `for` loop that day:

```c
#define T0_RELOAD (65536UL - (FOSC_HZ / 12UL / 1000UL))

static void delay_init(void)
{
    AUXR &= ~0x80;                  /* T0x12 = 0 -> Timer 0 clocked at FOSC/12 */
    TMOD  = (TMOD & 0xF0) | 0x01;   /* Timer 0, mode 1 (16-bit)                */
}

static void delay_ms(unsigned int ms)
{
    while (ms--) {
        TL0 = (unsigned char)(T0_RELOAD & 0xFF);
        TH0 = (unsigned char)(T0_RELOAD >> 8);
        TF0 = 0;
        TR0 = 1;
        while (!TF0) ;              /* wait for the 16-bit counter to overflow */
        TR0 = 0;
        TF0 = 0;
    }
}
```

The STC12 core is 1T and *could* clock Timer 0 straight off FOSC, but we keep
the traditional ÷12 prescaler (`AUXR.T0x12 = 0`) so that one millisecond still
fits in a 16-bit reload at any sensible clock. At 11.0592 MHz that is
11059200 ÷ 12 ÷ 1000 = 921 ticks, so `T0_RELOAD` = 65536 − 921 = 64615 =
`0xFC67` — and that is literally what ends up in the generated assembly.

### Tuning `FOSC`

If you have **no crystal fitted**, the internal RC oscillator is somewhere
between 11 and 17 MHz and your "1 second" will be off. Time the 1 Hz phase and
rebuild:

```bash
make flash FOSC=13000000
```

`FOSC` propagates into the compiler as `-DFOSC_HZ=…UL`, so nothing needs editing.

---

## 5. Build and flash

```bash
make                    # compile -> build/stc12c5a60s2/01-blink/01-blink.hex
make ports              # which /dev/cu.* looks like the adapter?
make flash              # build + flash, then power-cycle when prompted
```

Expected output:

```
$ make
sdcc -mmcs51 --std-c99 --iram-size 256 --xram-size 1024 --code-size 61440 \
     -I include -DFOSC_HZ=11059200UL -o build/stc12c5a60s2/01-blink/ src/01-blink/main.c
packihx build/stc12c5a60s2/01-blink/main.ihx > build/stc12c5a60s2/01-blink/01-blink.hex
packihx: read 19 lines, wrote 27: OK.
built build/stc12c5a60s2/01-blink/01-blink.hex
```

308 bytes of code, out of 61440 available.

```
$ make flash PORT=/dev/cu.usbserial-1420
Flashing build/stc12c5a60s2/01-blink/01-blink.hex via /dev/cu.usbserial-1420 ...
>>> Power-cycle the MCU now (unplug/replug its VCC) <<<
Waiting for MCU, please cycle power: done
Target model:
  Name: STC12C5A60S2
  Magic: F002
  Code flash: 60.0 KB
  EEPROM flash: 1.0 KB
Loading flash: 308 bytes
Switching to 115200 baud: done
Erasing flash: done
Writing flash: 308/308 bytes
Setting options: done
Disconnected!
```

### Compiling without installing anything

If you would rather not install SDCC — or you are driving this from a browser —
there is a hosted compiler at **<https://stc-compiler.vercel.app>**:

```bash
./tools/compile-remote.sh                  # 01-blink -> 01-blink.hex
FOSC=12000000 ./tools/compile-remote.sh    # override the clock
```

```
==> Amalgamating 01-blink ...
==> Compiling via https://stc-compiler.vercel.app (FOSC=11059200) ...
wrote 01-blink.hex (740 bytes)
  ROM/EPROM/FLASH 0x0000 0x00ed 238 61440
```

Then flash it exactly as usual with `stcgal`.

**It is not the same compiler as the one above, and the image is not the same
image.** The hosted service runs **SDCC 4.0.0**; `brew install sdcc` gives you
**4.5.0**. That is deliberate on their side — the service runs on a glibc 2.34
host, and 4.5.0 needs GLIBC 2.36 and will not start there — but it means a
remote build and a local build of one program are different firmware. Measured
on `01-blink`, same C and the same flags:

| built by | size |
|---|---|
| local SDCC 4.5.0 | 996 bytes |
| hosted SDCC 4.0.0 | 888 bytes |

Both work. But do not compare a remote `.hex` against a local one and conclude
something is wrong, and do not pair a remote image with a locally produced
symbol table. The page at <https://crispstrobe.github.io/stc-compiler/> now
names the compiler beside the byte count for this reason. The fix in progress
is SDCC compiled to WebAssembly, which runs in the browser with no glibc to be
pinned by — at which point both paths are 4.5.0 and this note goes away.

The service compiles **one translation unit**, so you cannot POST
`src/01-blink/main.c` directly — it would fail on `board.h: No such file`. The
script handles that: it strips the local `#include "..."` lines and
concatenates `include/board.h`, `include/delay.h` and the example into a single
file. System includes like `<stc12.h>` are left alone, since the server has
SDCC's own.

Source is at [`CrispStrobe/stc-compiler`](https://github.com/CrispStrobe/stc-compiler).
It exists because the BrickWright back-end in [docs/ROADMAP.md](docs/ROADMAP.md)
has to compile from a browser, where SDCC cannot run — the same reason
`legacy-lego-compiler` exists for NXT and EV3 bytecode.

### Writing it as pseudocode instead

The same programs also exist as **pseudocode** in [`pseudocode/`](pseudocode/),
in the dialect BrickWright uses — UPPERCASE for structure and control flow,
lowercase for statements, indentation for nesting:

```
DEVICE STC12C5A60S2:
  CLOCK 11059200

  PIN led1 = P1.0 OUTPUT ACTIVE LOW      # PDIP-40 pin 1
  PIN led2 = P1.1 OUTPUT ACTIVE LOW      # PDIP-40 pin 2

  WHEN started:
    FOREVER:
      turn on led1
      turn off led2
      wait 0.15 seconds
      turn off led1
      turn on led2
      wait 0.15 seconds
```

```bash
./tools/compile-remote.sh -p 01-blink        # -> 01-blink.hex + 01-blink.c
./tools/compile-remote.sh -p 02-button       # button toggles the LEDs
./tools/compile-remote.sh -p 03-potentiometer
```

It writes the generated C next to the `.hex`, so nothing is hidden — the
output is the same shape as the hand-written examples in `src/`.

`ACTIVE LOW` is the piece that matters. Because a quasi-bidirectional pin
sinks 20 mA but sources only ~230 µA (§1), LEDs get wired active-low and
`turn on` has to emit a `0`. Declaring the polarity once means the rest of the
program says what it means. The full grammar — `DEFINE` procedures, `WHILE`,
`REPEAT UNTIL`, `wait until`, `ANALOG` pins that read through the ADC — is
documented in the
[stc-compiler README](https://github.com/CrispStrobe/stc-compiler#pseudocode).

This is the front half of the BrickWright back-end in
[docs/ROADMAP.md](docs/ROADMAP.md): once blocks emit this pseudocode, the
whole chain from a Scratch project to a flashed chip is already in place.

### All the make targets

| Target | Does |
|---|---|
| `make` | Compile to `build/$(EXAMPLE)/$(EXAMPLE).hex` |
| `make flash` | Compile, then flash over ISP |
| `make info` | Read chip ID / options, flash nothing — the best connectivity test |
| `make erase` | Erase flash |
| `make ports` | List candidate serial devices |
| `make size` | Show the memory map from SDCC |
| `make clean` | Delete `build/` |

### All the knobs

| Variable | Default | Meaning |
|---|---|---|
| `EXAMPLE` | `01-blink` | Which directory under `src/` to build |
| `PART` | `stc12c5a60s2` | Target chip — also `stc15f2k60s2`. Sets the XRAM size, the stcgal protocol, and the build directory |
| `FOSC` | `11059200` | Clock in Hz — must match reality or delays are wrong |
| `PORT` | first matching `/dev/cu.*` | Serial device |
| `BAUD` | `115200` | Transfer rate. Drop to `19200` if flashing is flaky |
| `HANDSHAKE` | `2400` | Bootloader handshake rate — leave alone |
| `PROTOCOL` | `stc12` | stcgal protocol. `auto` also works |

### Flashing from a browser, without a terminal

<https://crispstrobe.github.io/stc-compiler/> transpiles pseudocode in the page
and can write the result to a board over Web Serial — an STC12 over its ISP
(cold power-on and all), an ATmega over the Arduino bootloader, a micro:bit
over the MicroPython REPL. Chrome or Edge only; Web Serial needs a secure
context, which the hosted page has and a local copy of the file does not.

**None of those three paths has yet programmed real hardware.** They are
developed against simulators, and the STC one is checked byte for byte against
a transcript `stcgal` itself produced — which establishes that the bytes are
right and nothing about whether the wire is. `make flash` remains the way that
is known to work. [docs/BENCH-FLASHING.md](docs/BENCH-FLASHING.md) is the
procedure for settling it, and names what to suspect first for each board.

---

## 6. Troubleshooting

**`Waiting for MCU, please cycle power:` never completes**

The most common failure, and it is almost always one of five things:

1. **TX/RX not crossed.** Adapter TXD → pin 10, adapter RXD → pin 11. Swap
   them and try again; you cannot damage anything by getting this wrong.
2. **You pressed reset instead of cycling power.** The ISP monitor only runs
   after a *cold* boot. Actually interrupt VCC.
3. **You are on `/dev/tty.*`.** Use `/dev/cu.*`.
4. **No common ground** between adapter and MCU.
5. **Something else has the port open** — a serial monitor, screen, the
   Arduino IDE. Close it.

**It connects but writing fails partway**

Drop the transfer rate: `make flash BAUD=19200`. Also check your USB cable —
charge-only cables are a classic time sink.

**Flashed fine, LEDs do nothing**

* Are the LEDs in backwards? Cathode (flat side / short leg) goes to the **MCU
  pin**, anode through the resistor to **+5 V**.
* Is RST (pin 9) actually low? If it floats high the chip sits in reset.
* Confirm the chip is alive: `make info` still works even with broken LED wiring.

**LEDs are lit but dim, or one is much dimmer**

You are probably still in quasi-bidirectional mode and source-driving them.
Check that `board_init()` is being called, and that the LEDs are wired
active-low as in §3.4.

**Blink rate is wrong**

`FOSC` doesn't match your actual clock. See §4.

**`sdcc: command not found` after `brew install sdcc`**

`brew` installed to `/opt/homebrew/bin` on Apple Silicon; make sure that is on
your `PATH`. `stcgal` from `pipx` lands in `~/.local/bin` — run
`pipx ensurepath` and open a new shell.

---

## 7. Repo layout

```
.
├── Makefile                 build / flash / erase / info
├── README.md   README.de.md this file, English and German
├── include/
│   ├── board.h              pin map, LED polarity, port-mode setup
│   ├── delay.h              Timer-0 millisecond delay
│   ├── live-proto.h         the debug link's wire format
│   ├── live-frame.h         its codec — plain C, so the host can test it
│   └── live-sfr.h           the curated SFR window (see below)
├── src/
│   ├── 01-blink/main.c      the C example
│   ├── 02-adc/main.c        ADC check — UNVERIFIED on silicon
│   └── 10-live-firmware/    on-chip debug monitor — UNVERIFIED on silicon
├── pseudocode/
│   └── *.bw                 the same, as BrickWright pseudocode
├── tests/
│   └── frame_test.c         the codec, tested on the host: make test
├── tools/
│   ├── setup-macos.sh       installs sdcc + stcgal
│   ├── find-port.sh         guesses the serial device
│   ├── compile-remote.sh    builds via the hosted compiler, no SDCC needed
│   └── live-monitor.py      the host end of the debug link
└── docs/
    ├── PINOUT.md   PINOUT.de.md    full pin + SFR reference
    ├── ROADMAP.md                 the BrickWright extension plan
    ├── STC12-PERIPHERAL-MODEL.md   what this chip does — the shared contract
    ├── DEBUG-CONTROL-MODEL.md      run control, for emulators and for silicon
    └── BENCH-FLASHING.md           verifying the browser flashers on silicon
```

### Debugging on real silicon

There is no on-chip debug unit on this part, and no `PSEN` pin, so the Keil
Monitor-51 approach — patch a trap opcode into code memory — is not merely
inconvenient here, it is unbuildable. `src/10-live-firmware` does what the
silicon *does* allow: it halts and steps at the cooperative scheduler's own
yield points, sets breakpoints there, and reads memory, registers and
position over UART1. `docs/DEBUG-CONTROL-MODEL.md` says exactly which
debugger features survive that and which do not.

The framing codec is plain C with no SFRs in it, so `make test` exercises the
same parser that runs on the chip, and diffs it against an independent Python
implementation in `tools/live-monitor.py`. **The firmware itself has not been
run on hardware.**

Every document exists in English and German. Source comments and identifiers
stay English, since they track the datasheet's own naming.

---

## 8. Where this is going

Blinking two LEDs was step zero. The primitives — GPIO, PWM (PCA 8-bit and
16-bit compare/match), ADC, UART, timers — are **built and cross-checked
between two emulators** (category 2b; not yet verified on silicon — see
`BENCH-ADC`, `BENCH-PWM`, `BENCH-UART` in [docs/BENCH-SESSION.md](docs/BENCH-SESSION.md)).

These primitives are exposed as **BrickWright blocks** that transpile to C and
compile to a `.hex` with the same SDCC + stcgal pipeline documented above.
Fourteen device blocks (servo, motor, relay, LCD, 7-segment, RGB LED, NeoPixel,
matrix, sensors) have real C drivers; the round-trip `pseudocode → C →
pseudocode` is a verified fixed point over 54 gallery examples.

```
   Scratch blocks ──▶ BrickWright IR ──▶ C (SDCC) ──▶ .ihx ──▶ stcgal ──▶ chip
```

The full design — block vocabulary, IR mapping, resource allocation, and how
flashing gets driven from the browser — is in [docs/ROADMAP.md](docs/ROADMAP.md).

### 8.1 One family is not the other: the 1T/12T trap

The STC12C5A60S2 drops **pin-for-pin into an STC89C52 socket** — power,
ground and the standard I/O all line up, and the same is broadly true the
other way around. The catch is time, not wiring: the STC12 (and the STC15)
are **1T** cores, the STC89 and every classic 8051 are **12T**. Code that
counts cycles — nested `for`-loop delays, `_nop_()`-timed bit-banging of
I2C/SPI/1-wire — runs roughly **6–12× too fast** after the swap and simply
stops working against real peripherals (a DS18B20 will not answer a 1-wire
reset that is twelve times too short).

Three consequences are baked into the tooling:

- **Everything generated from pseudocode is timed off Timer 0 at FOSC/12**,
  a mode that 12T and 1T parts count identically — so the same program is
  timing-correct on an `STC12C5A60S2`, an `STC89C52RC` or an
  `STC15F2K60S2` (all three are valid `DEVICE` choices, and the emitter
  knows which of them have port-mode registers, the AUXR 1T bit, or an ADC).
- **A wait shorter than a millisecond is refused, not rounded.** The tick is
  one millisecond, so `wait 0.4 ms` cannot be expressed. It used to compile
  to a wait of zero — the delay simply vanished and the loop ran flat out,
  with nothing in the output to say so. Since 2026-08-09 the compiler stops
  and names the resolution instead. Note the boundary is **half a millisecond
  inclusive**: `wait 0.5 ms` is refused too, because rounding a half goes to
  even and 0.5 rounds to 0. Sub-millisecond dwell — a POV cube's layer time,
  say — needs a microsecond delay that does not exist yet, and when it is
  built it will be timer-derived rather than a counted loop, for the reason
  this whole section is about.
- **The Keil translator warns** when migrated code contains software
  busy-wait loops or `_nop_()` runs, naming exactly this trap.

---

## References and prior art

* [STC12C5A60S2 / STC12LE5A60S2 datasheet (English, 2011-07-15)](https://www.stcmicro.com/datasheet/STC12C5A60S2-en.pdf) — the authoritative source for every number in §1
* [grigorig/stcgal](https://github.com/grigorig/stcgal) — the ISP flasher (MIT)
* [SDCC](https://sdcc.sourceforge.net/) — the compiler; ships `mcs51/stc12.h` (GPL-2+ with linking exception)
* [rgm3/ledcube444](https://github.com/rgm3/ledcube444) — 4×4×4 LED cube on this exact chip, and where the DTR auto-reset trick comes from (MIT)
* [tomazas/ledcube8x8x8](https://github.com/tomazas/ledcube8x8x8) — 8×8×8 cube, larger SDCC codebase (MIT)
* [kabirz/c51_sdcc](https://github.com/kabirz/c51_sdcc) — CMake scaffolding for SDCC + STC12 (MIT)
* [treideme/stc89c52-demos](https://github.com/treideme/stc89c52-demos) — 16 STC89C52 programs (Apache-2.0); the corpus the dialect's coverage was measured against, and the source of two examples here (see [NOTICE](NOTICE))

> **A note on licensing of third-party STC code.** Most STC12C5A60S2 code on
> GitHub carries **no licence at all** — including
> `Alpha02/MCU_STC12C5A60S2Lib`, `zeimao77/stc12` and
> `cch1997/SDCC-STC-header-file`. "No licence" means all rights reserved, so
> none of it can be vendored here. STC MCU Limited publishes no open-source
> repository either — only the datasheet PDF and the Windows-only, proprietary
> STC-ISP.exe. That is why this repo depends on **SDCC's own `stc12.h`** for
> register definitions: it is properly licensed, it ships with the compiler,
> and its addresses match the datasheet exactly. Everything else here is
> written from the datasheet, which is fact, not code.

## Licence

MIT — see [LICENSE](LICENSE) — with one exception.

`examples/08-seven-segment` and `examples/09-shift-register` are derived from
[treideme/stc89c52-demos](https://github.com/treideme/stc89c52-demos), which is
**Apache-2.0**. Both are modified: re-expressed in the pseudocode dialect, for a
different part, through a different toolchain. That licence travels with them —
its text is in [LICENSES/Apache-2.0.txt](LICENSES/Apache-2.0.txt) and the
attribution is in [NOTICE](NOTICE) as well as in each file's own header.
