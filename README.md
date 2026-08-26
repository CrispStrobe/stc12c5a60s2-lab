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
9. [First silicon — what is now verified on real hardware](#9-first-silicon--what-is-now-verified-on-real-hardware)

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

> [!NOTE]
> **For an STC12 the choice may be narrower than this table.** stcgal's FAQ
> keeps its own list of tested bridge chips, and **CP2102 is the only one with
> macOS listed as a tested platform**; CH340/CH341 are recorded for Windows and
> Linux only. The STC12 bootloader requires **even parity** (§6), unlike the
> STC89 family. On 2026-08-26 a CH341T (`1a86:5523`) on macOS 26 accepted an
> 8E1 configuration and passed a byte-exact loopback test, and the same UART
> path identified an STC89. That verifies the local driver and wiring, but not
> STC12 compatibility: no STC12 bootloader reply was obtained.

Some modules have **two** jumpers: one for voltage (3.3 V / 5 V) and one for
mode (TTL / I²C). On a CH341T module both must be right — **5 V** and **TTL**.
At 3.3 V the STC12 is below its 3.5 V minimum, and its V<sub>IH</sub> of
0.7 × VCC = 3.5 V would be out of reach anyway.

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
3. `stcgal` catches the handshake, negotiates a transfer rate, and
   writes the flash.

> [!IMPORTANT]
> **2400 is stcgal's default, not the only valid rate.** Successful sessions
> with this exact part exist at 2400, 4800 and 9600 baud. One report needed
> `-l 9600`, while `my1stcflash` uses 2400. Start with the default and try
> `HANDSHAKE=9600 BAUD=9600` if detection fails; details are in §6.

If you want to automate step 2, wire the adapter's **DTR** line to a
transistor that switches the MCU's VCC, and add `-a` to the stcgal call — see
[rgm3/ledcube444](https://github.com/rgm3/ledcube444) for a photo of exactly
that hack. Until then, a small switch or just pulling the VCC jumper is fine.
Not every module brings DTR out at all: a CH341T has only TXD/RXD/GND/VCC
(plus SDA/SCL), so `-a` is not an option there. Whether control lines exist is
one line to check — `serial.Serial(...).cts` / `.dsr`; all `False` means there
are none.

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
| *opt.* | 1 kΩ resistor — series protection in adapter TXD |
| *opt.* | 470 Ω resistor — VCC-to-GND bleeder for defeating phantom power |
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
  run. A 1 kΩ resistor to GND is sufficient below 12 MHz because the chip has
  an internal power-on reset. The traditional 10 kΩ-to-GND plus 10 µF-to-VCC
  circuit is also active-high: it creates a short high pulse at power-up. It
  is shown in STC's application circuit, but toggling RST does not enter ISP;
  ISP still requires a real power-on event.
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
        TXD  ──[ 1k optional ]─▶  pin 10   P3.0 / RxD
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

If you power from the adapter, leave pin 40 permanently connected to the
target VCC rail and remove/reapply the adapter's 5 V feed to that rail. UART
pins can phantom-power the MCU even with the 5 V feed removed. A 1 kΩ series
resistor in TXD and a 470 Ω bleeder from target VCC to GND are useful on a
breadboard. Before trusting a cold-start test, measure pin 40 to pin 20: it
must fall well below the 3.5 V operating range, preferably below 1 V. Merely
moving a jumper is not evidence that the chip actually powered off.

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
  Magic: D17E
  Code flash: 60.0 KB
  EEPROM flash: 2.0 KB
Loading flash: 308 bytes
Switching to 115200 baud: done
Erasing flash: done
Writing flash: 308/308 bytes
Setting options: done
Disconnected!
```

`stcgal` reports 2 KB for magic `D17E`, while STC's selection table describes
1 KB of user EEPROM. The sample above mirrors the tool; the feature summary
follows the vendor table. Treat the discrepancy conservatively until it is
verified on STC12 silicon. The old sample incorrectly showed the STC89 entry
`F002` here.

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
| `HANDSHAKE` | `2400` | Bootloader handshake rate; try `4800` or `9600` if detection fails |
| `PROTOCOL` | `stc12` | stcgal protocol. `auto` also works |

### Flashing from a browser, without a terminal

<https://crispstrobe.github.io/stc-compiler/> transpiles pseudocode in the page
and can write the result to a board over Web Serial — an STC12 over its ISP
(cold power-on and all), an ATmega over the Arduino bootloader, a micro:bit
over the MicroPython REPL. Chrome or Edge only; Web Serial needs a secure
context, which the hosted page has and a local copy of the file does not.

The browser/Web Serial STC12 path has not yet programmed real STC12 hardware.
It is checked byte for byte against a transcript `stcgal` produced, which
establishes that the bytes are right and nothing about whether the wire is.
The command-line JavaScript and Rust flash engines *have* both programmed an
STC89 on real hardware; see §9 and
[docs/BENCH-FLASHING.md](docs/BENCH-FLASHING.md).

---

## 6. Troubleshooting

**`Waiting for MCU, please cycle power:` never completes**

The most common failure, and it is almost always one of five things:

1. **TX/RX not crossed.** Adapter TXD → pin 10, adapter RXD → pin 11. Swap
   them and try again; you cannot damage anything by getting this wrong. Some
   modules label the header from the *target's* point of view, though, in which
   case straight-through is correct — see the STC12 section below.
2. **You pressed reset instead of cycling power.** The ISP monitor only runs
   after a *cold* boot. Actually interrupt VCC.
3. **You are on `/dev/tty.*`.** Use `/dev/cu.*`.
4. **No common ground** between adapter and MCU.
5. **Something else has the port open** — a serial monitor, screen, the
   Arduino IDE. Close it.

**An STC12 stays silent though the same adapter flashes an STC89**

From a bench session on 2026-08-09 that did **not** succeed: the chip never
answered, and what was actually wrong is still open. The points below are
evidenced anyway — each from stcgal's source, the datasheet, or other people's
session logs. They are recorded because together they cost several hours.

**YL-39 board controls are not STC12 protocol selectors.** The available
[YL-39 manual](https://www.100y.com.tw/pdf_file/57-YL-39.pdf) assigns JP1 to
the 8-LED bank, JP2 to the four-digit display and JP3 to the buzzer. They only
connect peripherals and should be removed when those peripherals are unused.
The separate `51/AVR` selector belongs in the `51` position. Programming is
the ordinary STC UART sequence: turn board power off, start Download in
STC-ISP, wait briefly, then turn board power on. The manual suggests setting
both minimum and maximum baud to 2400 if download detection fails.

There are materially different boards sold under this description. That
manual advertises USB download for an `STC52` and names a PL2303 interface;
the board tested here and current marketplace text name a CH340G and advertise
the STC12C5A60S2. No documentation found describes a hidden STC12 jumper or a
second programming connector. STC12 support therefore means socket/pinout,
crystal, power switch and UART compatibility—not a different download mode.
One verified-purchase review also reports receiving boards that did not match
the listing photographs, so identify the actual PCB revision rather than
trusting the title or AI-generated overview.

**Do not assume one handshake rate.** stcgal defaults to 2400 and
`my1stcflash`, tested by its author on this exact model, also uses 2400.
stcgal's protocol capture uses 9600, and the successful session in
[stcgal#12](https://github.com/grigorig/stcgal/issues/12) first connected with
`-l 9600`; its later baud matrix connected at 2400, 4800 and 9600. Try the
default first, then:

```bash
make info HANDSHAKE=9600 BAUD=9600
```

**The STC12 bootloader speaks 8E1; the STC89 speaks 8N1.** In stcgal's source,
`Stc12BaseProtocol.PARITY = serial.PARITY_EVEN` ("Parity for error correction
was introduced with STC12") against `Stc89Protocol.PARITY = PARITY_NONE`
("these don't use any parity"). An uncomfortable consequence follows: **the
CH340 path proven on silicon in §9 never exercised parity at all.** It says
nothing about an STC12. stcgal's FAQ names exactly one known total failure —
the Raspberry Pi Mini UART — precisely because it *"lacks parity support"*, and
in its table of tested bridge chips **CP2102 is the only one with macOS** in
the column. If you write your own capture tool, open the port with
`PARITY_EVEN` too, or you are testing something other than what stcgal does.

**stcgal 1.10 prints no progress dots while waiting.**
`StcBaseProtocol.connect()` writes `Waiting for MCU, please cycle power:` once
and then stays silent until a valid status packet arrives; framing errors are
swallowed. A dotless prompt is the normal state, not a symptom — and it cannot
distinguish "nothing comes back" from "garbage comes back". To separate those,
write six lines that send `0x7f` and report *every* received byte. A valid
reply begins `46 B9`.

**A removed VCC wire does not prove that power is off.** Adapter TXD can feed
the chip through the clamp diode on RxD. In the 2026-08-26 session pin 40 still
measured about 5 V after the adapter's 5 V feed was removed. Use series
resistance in TXD and, if necessary, a bleeder of less than 1 kΩ from target
VCC to GND, as stcgal's FAQ recommends. Keep pin 40 connected to the target
VCC rail and switch the supply *upstream* of that rail so the bleeder and both
decoupling capacitors remain connected to the MCU while off. Verify the result
at the chip: pin 40 to pin 20 must fall below the operating range before the
next rising edge.

Switching target GND with a proper low-side MOSFET is another documented
solution. Pulling a random GND jumper by hand is not equivalent when LEDs,
program-entry straps, crystal capacitors or other board wiring provide return
paths.

> [!CAUTION]
> Do **not** short the supply rails to drain the bulk capacitor while the
> adapter's 5 V wire is still connected. That is a dead short across VBUS: the
> USB port shuts down, the device vanishes from `/dev`, and pyserial reports
> `OSError: [Errno 6] Device not configured`.

**TXD/RXD silkscreens are not trustworthy.** Some CH34x modules label the
header from the *target's* perspective; in
[ledcube8x8x8#8](https://github.com/tomazas/ledcube8x8x8/issues/8) only
TXD→TXD / RXD→RXD worked. A loopback test (join the two pins, read the bytes
back) proves they are a UART pair — **not** which pin is which. When in doubt
try both orientations, at 9600/8E1.

**Check continuity on the actual IC legs after every swap.** A bent pin can
look seated while missing the breadboard contact. During the 2026-08-26
session an STC89 that had just been the positive control became completely
silent after a swap because pin 40 was bent. Probing the breadboard row looked
correct; probing the exposed metal of pin 40 did not. With all power removed,
each supply leg must measure approximately 0 Ω to its own breadboard row.

**A used chip may be set to an external clock.** If the `clock_source` option
byte says `external` and there is no crystal on pins 19/18, nothing runs at
all — not even the bootloader — and the chip sits silent with perfect DC
readings on every pin. Every findable success report for this part had a
crystal fitted (stcgal#12: 11.059 MHz; ledcube8x8x8#8: 23.846 MHz). §3.2 says
"XTAL may stay empty"; that holds for a factory-fresh chip, not necessarily for
a marketplace one.

**`bsl_pindetect_enabled` can gate ISP entry on P1.0/P1.1.** With that option
byte set, the bootloader only enters ISP if both pins are low at power-on. Two
jumpers to GND cost nothing to rule out.

**Supply current is the fastest liveness test.** Meter in mA range, in series
with the 5 V wire: **10–25 mA** means the oscillator is running and the CPU is
executing; **under 1 mA** means there is no clock. That separates "miswired"
from "chip isn't running" in a single measurement, faster than any per-pin
voltage reading — and in the session above it was the measurement that came far
too late. (First confirm the mA range works at all, across a 1 kΩ to 5 V:
~5 mA. A blown fuse in the mA path reads 0.0 and is indistinguishable from
"draws nothing".)

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

**An STC89C52 answers `make info` but refuses to flash (`PROTOCOL=stc89`)**

Newer STC89C52RC production batches ship a bootloader revision that mainline
stcgal's `stc89` protocol does not recognise. Try `PROTOCOL=stc89a` first;
if your stcgal is too old to know `stc89a`, update it (the variant was merged
after a long-lived fork carried it). The chip is fine — it is purely a
handshake-dialect mismatch, and `PROTOCOL=auto` may also negotiate it.

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
│   ├── 02-adc/main.c        ADC check — UNVERIFIED (needs STC12 silicon)
│   ├── 03…09,11-13/         bench-verified STC89 examples: blink, two-way
│   │                        UART, board discovery, matrix scan, 7-seg,
│   │                        LED find, keypad-to-display, dot matrix, LCD
│   │                        probe, Timer-2 baud at 115200 — see §9
│   └── 10-live-firmware/    on-chip debug monitor — UNVERIFIED (STC12)
├── pseudocode/
│   └── *.bw                 the same, as BrickWright pseudocode
├── tests/
│   └── frame_test.c         the codec, tested on the host: make test
├── tools/
│   ├── setup-macos.sh       installs sdcc + stcgal
│   ├── find-port.sh         guesses the serial device
│   ├── compile-remote.sh    builds via the hosted compiler, no SDCC needed
│   └── live-monitor.py      the host end of the debug link
├── tools/stcbsl/            our own ISP flasher in Rust (MIT) — working
│                            tree; released at github.com/CrispStrobe/stcbsl
│                            and on crates.io as `stcbsl`
└── docs/
    ├── PINOUT.md   PINOUT.de.md    full pin + SFR reference
    ├── ROADMAP.md                 the BrickWright extension plan
    ├── STC12-PERIPHERAL-MODEL.md   what this chip does — the shared contract
    ├── DEBUG-CONTROL-MODEL.md      run control, for emulators and for silicon
    ├── BOARD-PRECHIN-A2.md         a real board, measured pin by pin
    ├── isp-captures/               byte-exact ISP session logs (stcbsl's spec)
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
between two emulators**; as of the first bench sessions (§9), GPIO, UART and
the timers are verified on real STC89 silicon, while ADC and PCA/PWM still
wait for an STC12 in the socket (see `BENCH-ADC`, `BENCH-PWM` in
[docs/BENCH-SESSION.md](docs/BENCH-SESSION.md)).

These primitives are exposed as **BrickWright blocks** that transpile to C and
compile to a `.hex` with the same SDCC + stcgal pipeline documented above.
Fourteen device blocks (servo, motor, relay, LCD, 7-segment, RGB LED, NeoPixel,
matrix, sensors) have real C drivers; the round-trip `pseudocode → C →
pseudocode` is a verified fixed point over 54 gallery examples.

```
   Scratch blocks ──▶ BrickWright IR ──▶ C (SDCC) ──▶ .ihx ──▶ stcgal ──▶ chip
                                     └─▶ MicroPython ──▶ bw flash ──▶ Pi Pico
```

The second row is not hypothetical — see §9.1. The same pseudocode reaches an
8051 through SDCC and an RP2040 through MicroPython, which is the whole point
of putting an IR in the middle.

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

## 9. First silicon — what is now verified on real hardware

On 2026-08-17/18 the first bench sessions ran, on two STC89C52RC boards (a
minimum-system board and a Prechin 普中51-单核-A2). Everything below moved
from "cross-checked between two emulators" to **measured on a real chip**:

- **The whole toolchain**: `make` → SDCC → stcgal over a CH340, from macOS,
  no Windows tool anywhere. Flash, erase, identify — dozens of sessions.
- **Timers**: Timer 0 at FOSC/12, crystal-true (a stopwatch-verified 1 Hz
  blink); Timer 1 mode 2 as 9600-baud clock, two-way UART proven by the
  host reading the firmware's output back over the same cable.
- **Timer 2 as baud generator at 115200** (`src/13-hello115`): reload
  `0xFFFD` = 11 059 200 / 32 / 3 — byte-perfect at the host. This is the
  fact the planned STC89 port of the live monitor stands on.
- **Blocks to silicon**: BrickWright pseudocode → the hosted compiler →
  a running chip, including the dialect's `PART KEYPAD4X4` and `PART
  74HC595` on measured pins. The A2's keypad, 7-segment, LED bank and
  8×8 matrix were mapped **by firmware, not by datasheet trust** —
  [docs/BOARD-PRECHIN-A2.md](docs/BOARD-PRECHIN-A2.md) holds every
  measured fact, including the J24 output-enable jumper that cost five
  dark flashes.
- **Our own flasher**: [`stcbsl`](https://github.com/CrispStrobe/stcbsl)
  (MIT, Rust, [on crates.io](https://crates.io/crates/stcbsl)) speaks the
  STC89 ISP protocol end to end at full 115200, built from byte-exact
  session captures ([docs/isp-captures/](docs/isp-captures/)) and debugged
  against silicon through five real bugs — including macOS's CH340 driver
  silently ignoring bare-termios baud changes, which pyserial masks with
  an `IOSSIOSPEED` ioctl and every Rust serial program should know about.
- **Three visibly different CLI flash paths on 2026-08-26**: the npm CLI's
  built-in JavaScript flasher installed a D3–D8 walk; npm `--engine rust`
  installed alternating odd/even LEDs; and direct
  `stcbsl flash program.bw` transpiled pseudocode through its embedded
  JavaScript engine, invoked SDCC and installed alternating D1–D4/D5–D8 in
  one command. Each changed image was observed on the YL-39 STC89 board.

The STC12 session on 2026-08-26 did **not** establish a working STC12 path.
Three chips, all marked `12C5A60S2 35I-PDIP40 1901H4Y043`, stayed silent in a
YL-39, a Prechin A2 and direct CH341T breadboard tests. The investigation found
real confounders—phantom power, a split/indirect breadboard power path and a
bent pin 40 after repeated swaps. The final CH341T circuit identified the
STC89 through a 1 kΩ TXD resistor and a 470 Ω supply bleeder, but the planned
STC12 repeat after that positive control was deliberately aborted. Therefore
the common STC12 lot is suspect, but neither defective chips nor a broken
STC12 protocol implementation is proven. Known-working programmed STC12
chips were intentionally left untouched.

Still waiting for an STC12 in the socket (the boards' sockets accept one
pin-for-pin): the **ADC path** (`src/02-adc`), **PCA/PWM**, the **BRT**,
and the **live debug monitor** (`src/10-live-firmware`). Those stay
flagged UNVERIFIED until that bench day.

### 9.1 A second architecture: the Raspberry Pi Pico (2026-08-19)

The dialect is not 8051-only, and this is the proof. On 2026-08-19 a
**Raspberry Pi Pico** ran a pocket calculator written as BrickWright
pseudocode — seventeen direct-wired keys on GP2–GP18 and a **GME12864-70**
OLED (SH1106-class, 128×64, I²C on GP0/GP1), with the Pico's internal
pull-downs doing the work so a pressed key reads HIGH and no external
resistors are needed.

It was deployed by this project's own CLI, not by a vendor tool:

```bash
cd ../sb3-creator
node bin/bw.mjs flash examples/70-calculator/program.bw \
     --port /dev/cu.usbmodem11101
```

`bw flash` writes `main.py` over its own raw-REPL implementation, verifies the
write on the device and resets it. Reading the file back with `mpremote` gives
a **byte-identical** copy of what `bw transpile --to micropython` produces, so
what runs on the chip is exactly what the dialect generated. The calculator was
confirmed working on the bench by the owner.

Getting there took four fixes to the MicroPython back-end, each measured on the
chip rather than argued from the source. The most surprising: MicroPython's
compiler folds `if False:` away entirely, so the dead-yield trick that turns a
yield-less function into a generator silently produces an ordinary function
returning `None` — and every call through `yield from` dies with
`TypeError: 'NoneType' object isn't iterable` before the first frame is drawn.

**One trap worth repeating:** on a Pico, **physical pin 22 is GP17**. Header
numbering and GPIO numbering are different coordinate systems, and "pin 22" is
ambiguous until someone says which one they mean — the same discipline §1
asks for on the STC12.

Still unconfirmed on the bench: a later revision of the display code (one I²C
blit per frame instead of one per print, and a right-aligned entry line) is
flashed and boots cleanly, but has not been looked at with a human eye.

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
