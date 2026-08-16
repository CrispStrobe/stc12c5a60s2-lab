# The STC15 peripheral model — a delta against the STC12, not a second document

**Why a delta.** [`STC12-PERIPHERAL-MODEL.md`](STC12-PERIPHERAL-MODEL.md) already describes a
1T STC core with `PxM1`/`PxM0` port modes, a Timer 0 at FOSC/12, and a 10-bit ADC on P1. The
STC15F2K60S2 is the same picture with pieces added and a small number of pieces moved. Copying
the STC12 document and editing it would produce two texts that drift; this one says only what is
different, and everything it does not mention is unchanged and governed by that document.

Same rule as its parent: **implementations cite this, they do not re-derive it.** The same three
implementations are in scope — the ucsim fork, the emu8051 fork, and the on-chip monitor.

**Part in scope: `STC15F2K60S2`, and only that part.** The STC15 family is wide and the variants
genuinely differ. **The SFR address map in datasheet §3.3.1 is family-wide**, so reading a
register out of it proves only that *some* STC15 has it — which is the trap this section exists
to close. Datasheet §1.1.1 (p. 15–16) is the per-part feature list, and §3 below has been
reconciled against it. Every ⚠ that remains is a real open question, not an unread page.

Part identity, from §1.1.1:

| | STC15F2K60S2 | STC12C5A60S2 |
|---|---|---|
| Flash | 60 KB | 60 KB |
| SRAM | **2048 B** = 256 scratch-pad + **1792 auxiliary** | 1280 B = 256 + 1024 |
| Supply | **5.5–4.2 V** (`STC15L2K60S2` is 3.6–2.4 V) | 5.5–3.5 V |
| Core | 1T, STC-Y5 — ~20% faster than the STC12's 1T at the same clock | 1T |
| Max clock | 28 MHz | 35 MHz |
| ADC | 8 channels, 10-bit, to 300 ksps | 8 channels, 10-bit |
| UARTs | 2 (UART1/UART2), remappable across 5 pin groups | 2 |
| CCP/PCA/PWM | **3 channels** | 2 modules |

The SRAM difference is why `stc-compiler`'s target table already passes `--xram-size 1792` for
this part rather than the STC12's 1024. The supply difference matters on a bench: **an STC15F
wants 4.2 V minimum**, so it is less tolerant of a sagging USB rail than the STC12.

> [!CAUTION]
> **The two parts are not pin-compatible, and the supply pin is one of the ones that moves** —
> VCC is pin 40 on an STC12C5A60S2 and **pin 18** on an STC15F2K60S2 in the same PDIP-40
> package. This document is about registers; before wiring anything, read
> [`PINOUT-STC15.md`](PINOUT-STC15.md).

## How this was derived, and why that matters

Two independent sources, because the parent document's rule is that **an address is a fact only
when two sources agree**:

1. [STC15 series datasheet](https://www.stcmicro.com/datasheet/STC15F2K60S2-en.pdf), §3.3.1
   address map (p. 291) and §3.3.2 bit descriptions (p. 292).
2. A third-party generated header, [`stc15.h` by Vincent
   DEFERT](https://github.com/ghosoft/stc-mcu-open-source/blob/master/header-generator/stc15.h)
   (BSD-2-Clause), diffed mechanically against SDCC's `mcs51/stc12.h`.

**They disagreed, and the disagreement is instructive.** The header omits `BRT` at 0x9C
entirely, which would have produced a delta document claiming the STC15 has no baud-rate timer.
The datasheet's bit table lists it. Had this been written from the header alone — the faster,
more convenient source — the error would have propagated into two emulators and the firmware.

⚠ **And the datasheet contradicts itself about 0x9C.** The address map on p. 291 marks it
`Don't use`; the bit description table on p. 292 lists `BRT — dedicated Baud-Rate Timer — 9CH`.
Do not resolve this by picking one. Treat 0x9C as **present but deprecated on this part**, use
Timer 2 for baud (see below, which is what the AUXR bits say the part intends), and settle it on
silicon.

## 1. What is identical — the large majority

**74 SFRs share both name and address with the STC12, and *no* register keeps its name at a
different address.** There is no silent name-aliasing trap between these two families, which is
the single most useful thing to know here.

Confirmed unchanged and load-bearing for everything this toolchain generates:

- **`AUXR` bit 7 is `T0x12` on both.** Timer 0 at FOSC/12 with `AUXR.7 = 0`, and the 1 ms reload
  `65536 − FOSC/12/1000`, are identical. This is why one program is timing-correct on STC12,
  STC15 and STC89 — see the parent document §1.
- **Port modes**: `PxM1`/`PxM0`, same addresses, same four modes, same sink/source asymmetry and
  therefore the same active-low LED convention.
- **ADC core**: `P1ASF` 0x9D, `ADC_CONTR` 0xBC, `ADC_RES` 0xBD, `ADC_RESL` 0xBE — all unchanged.
  The register *sequence* in the parent document §4 applies unchanged. **But see §2 for `ADRJ`.**
- Timers 0 and 1, `TCON`, `TMOD`, `IE`, `IP`, `SCON`/`SBUF`, `S2CON`/`S2BUF`, `PCON`, and the
  whole classic 8051 core.

## 2. Same address, different meaning — the three traps

These are the ones that produce plausible, wrong behaviour rather than an obvious failure.

### 2.1 `ADRJ` moved — 0xA2 bit 2 → 0x97 bit 5

The parent document §4 puts ADC result justification in `AUXR1.ADRJ` (bit 2 of 0xA2). **On the
STC15 it is in `CLK_DIV`/`PCON2` (0x97) bit 5**, and 0xA2 has become the peripheral-switch
register:

| addr | STC12 | STC15 |
|---|---|---|
| 0x97 | `CLK_DIV` | `CLK_DIV`/`PCON2` — `MCKO_S1 MCKO_S1` **`ADRJ`** `Tx_Rx MCLKO_2 CLKS2 CLKS1 CLKS0` |
| 0xA2 | `AUXR1` — includes `ADRJ` at bit 2 | `AUXR1`/`P_SW1` — `S1_S1 S1_S0 CCP_S1 CCP_S0 SPI_S1 SPI_S0 0 DPS`, **no `ADRJ`** |

Code that sets ADRJ on an STC12 and is moved to an STC15 writes a **peripheral pin-remap
register** instead, and the ADC keeps its default alignment. Nothing errors.

Our generated code is safe by luck rather than design: it never *writes* `ADRJ` and both parts
reset it to 0, which is the alignment the emitter's
`(ADC_RES << 2) | (ADC_RESL & 0x03)` assumes. Anyone hand-writing ADC code, or translating Keil
code between the families, is not safe.

### 2.2 `AUXR`'s lower bits: the baud-rate timer became Timer 2

Top three bits and `EXTRAM` are identical; the BRT bits became Timer 2 bits.

| bit | STC12 | STC15 |
|---|---|---|
| 7 | `T0x12` | `T0x12` |
| 6 | `T1x12` | `T1x12` |
| 5 | `UART_M0x6` | `UART_M0x6` |
| 4 | `BRTR` | **`T2R`** |
| 3 | `S2SMOD` | **`T2_C/T`** |
| 2 | `BRTx12` | **`T2x12`** |
| 1 | `EXTRAM` | `EXTRAM` |
| 0 | `S1BRS` | **`S1ST2`** |

The *roles* survive — run the baud timer, clock it at 1T, select it as UART1's source — so
`AUXR |= 0x15` happens to mean the same thing on both. **The reload register does not survive:**
STC12 writes an 8-bit reload to `BRT` (0x9C); STC15 writes a 16-bit reload to `T2H`/`T2L`
(0xD6/0xD7). This is a concrete portability defect in `src/10-live-firmware`, recorded in §4.

### 2.3 `WAKE_CLKO` → `INT_CLKO`/`AUXR2` at 0x8F

Same address, related purpose, different bits: `EX4 EX3 EX2 – T2CLKO T1CLKO T0CLKO`. The STC15
adds external interrupts 2–4 and programmable clock output per timer.

## 3. What the STC15 adds

Nothing here exists on the STC12, so a *reader* of these addresses on an STC12 model should
refuse rather than return zero (the gating rule — see `DEBUG-CONTROL-MODEL.md` §6 for the same
principle applied to the on-chip monitor).

**Present on the STC15F2K60S2** — in the family address map *and* in this part's feature list:

| block | registers |
|---|---|
| **Timer 2** | `T2H` 0xD6, `T2L` 0xD7 — the UART1 baud source, and a general 16-bit auto-reload timer |
| **Third PCA/CCP module** | `CCAPM2` 0xDC, `CCAP2L` 0xEC, `CCAP2H` 0xFC, `PCA_PWM2` 0xF4 — this part has **3** CCP/PCA channels, the STC12 has 2 |
| **SPI** | `SPSTAT` 0xCD, `SPCTL` 0xCE, `SPDAT` 0xCF |
| **IAP / EEPROM** | `IAP_DATA` 0xC2, `IAP_ADDRH` 0xC3, `IAP_ADDRL` 0xC4, `IAP_CMD` 0xC5, `IAP_TRIG` 0xC6, `IAP_CONTR` 0xC7, `WDT_CONTR` 0xC1 |
| **Wake-up timer** | `WKTCL` 0xAA, `WKTCH` 0xAB — the low-power timer that wakes stop/power-down mode |
| **Peripheral switch** | `AUXR1`/`P_SW1` 0xA2, `P_SW2` 0xBA — UART1 remaps to (P3.6,P3.7) or (P1.6,P1.7), UART2 to (P4.6,P4.7) |
| **Bus speed** | `BUS_SPEED` 0xA1 |
| **Clock output / extra interrupts** | `INT_CLKO`/`AUXR2` 0x8F — `EX4 EX3 EX2 – T2CLKO T1CLKO T0CLKO` |
| **Port 5** | `P5` 0xC8 (bit-addressable — the 8052's T2CON slot), `P5M1` 0xC9, `P5M0` 0xCA |

**P5 in practice (added 2026-08-16, for the retro-console work):** on the
PDIP-40 only two P5 bits reach pins — **P5.4 on pin 17** (shared with RST;
an ISP option byte decides whether the pin is reset or I/O) and **P5.5 on
pin 19**. Port modes work like every other port (`P5M1`/`P5M0`, reset
default quasi-bidirectional). This is not a corner: the RBS15667 retro
console (docs/RETRO-CONSOLE-RBS15667.md) drives its buzzer transistor
from **P5.5**, so any toolchain or emulator that wants to run console
firmware must know P5 exists. Status (2026-08-16, all landed):
SDCC 4.5.0's `stc12.h` turned out to ALREADY declare `P5`/`P5M0`/
`P5M1` at these addresses (and sbits `P5_0..P5_3` — an LQFP-48 STC12
note); what it lacks, `sb3-creator` now emits as a complete "STC15
supplement" for every STC15 program (88c38318b): sbits `P5_4..P5_7`,
Timer 2, `P_SW2`, the wake-up timer, the third PCA channel, and the
STC15 names `P_SW1`/`INT_CLKO`. The PIN grammar accepts `P5.x` with
truthful refusals (no P5 on an STC12 part; unbonded-bit warning on
the DIP-40), the C reader parses P5 back, and emu8051-stc models the
port (182/0 assertions); ucsim-stc in flight. Owner directive stands:
header gaps get fixed TOTALLY, never per-feature.

**In the family map but NOT on this part** — a model for the STC15F2K60S2 must refuse these, not
implement them. This is the same rule Stage 0 applies to STC8 registers in an STC12 model:

| block | registers | why not |
|---|---|---|
| **Timers 3 and 4** | `T4T3M` 0xD1, `T4H` 0xD2, `T4L` 0xD3, `T3H` 0xD4, `T3L` 0xD5 | §1.1.1: "three 16-bit reloadable Timer/Counter (T0/T1/T2)". The "six timers" in the same sentence counts the 3 CCP/PWM channels used as timers, not T3/T4. |
| **UART 3 and 4** | `S3CON` 0xAC, `S3BUF` 0xAD, `S4CON` 0x84, `S4BUF` 0x85 | §1.1.1: "**Two** high-speed asynchronous serial ports — UART1/UART2". The "regarded as 5 serial ports" phrasing means 2 UARTs remappable across 5 pin groups, not 5 UARTs. Easy to misread. |
| **Enhanced PWM** | `PWMCFG` 0xF1, `PWMCR` 0xF5, `PWMIF` 0xF6, `PWMFDCR` 0xF7 | STC15W4KxxS4 only. This part's PWM comes from the CCP/PCA channels (3× 9–16 bit) and from timer clock output (8–16 bit), not from a dedicated block. |
| **Ports 6 and 7** ⚠ | `P6` 0xE8, `P6M1` 0xCB, `P6M0` 0xCC, `P7` 0xF8, `P7M1` 0xE1, `P7M0` 0xE2 | Absent from the feature list. ⚠ the pin-configuration section (§1.1.3, p. 19) would settle it definitively; the feature list not mentioning them is strong but indirect. |

Gone relative to the STC12: **`P4SW` (0xBB) is not present** — its job is done by `P_SW1`/`P_SW2`.

## 4. Consequences for what this repo has already built

- **`stc_pseudocode.py` is correct as it stands.** Its `PARTS` entry maps `stc15f2k60s2` to
  `stc12.h`, on the stated grounds that every register the emitter touches is at the same
  address. That claim now has a second source behind it: P0–P3, `PxM0`/`PxM1`, `AUXR.T0x12`,
  Timer 0, `P1ASF` and the ADC block are all confirmed identical. The comment's list of "famous
  divergences it never writes" was right, and `ADRJ` (§2.1) can be added to it.
- **`src/10-live-firmware` handles the baud-rate difference correctly.** The `#ifdef
  PART_STC15F2K60S2` path in `uart_init()` writes `T2H`/`T2L` (16-bit reload) instead of `BRT`
  (8-bit), and `AUXR |= 0x15` means the same thing on both parts (run the baud timer at 1T,
  select it as UART1's source). Verified under ucsim: both builds produce AUXR=0x15 and SCON=0x50
  after init, with the correct reload register populated (BRT=0xFD on STC12, T2 running from
  0xFFFD on STC15). The naive port (BRT written on STC15 without the `#ifdef`) would produce
  **5 baud instead of 115200** — 23,040× wrong — because 0x9C is deprecated and T2H/T2L stays
  at the reset default. See `ucsim-stc/spec-updates/015-baud-reload-table.md` for the full table.
- **The curated SFR window in `include/live-sfr.h` handles the STC15 correctly.** T2H/T2L are
  included when `PART_STC15F2K60S2` is defined, and P4SW is excluded.

## 5. The one genuine advantage: the clock stops being a guess

The parent document notes that the STC12's internal RC is 11–17 MHz and drifts, which is why
`FOSC` needs tuning by eye and why a crystal is recommended. The STC15 is a different situation
entirely, and this is the most practically useful difference between the two parts.

Datasheet §1.1.1: the internal R/C clock is **±0.3%**, with **±1% drift over −40…+85 °C** and
±0.6% over −20…+65 °C, adjustable across 5–35 MHz, and the datasheet names the canonical values:
**5.5296 / 11.0592 / 22.1184 / 33.1776 MHz**. It states plainly: "No need external crystal and
reset."

**11.0592 MHz — the exact frequency every `FOSC_HZ` default in this repo assumes — is one of
those canonical values**, and `stcgal -t 11059` sets it at download time ("RC oscillator
frequency in kHz, STC15+ series only"). So on an STC15, with no crystal fitted and no parts on
the board beyond power, the clock is a known quantity to a fraction of a percent.

That removes the single most annoying unknown in the STC12 workflow, and it is why the STC15 is
the better part for *verifying* any timing claim: on the STC12, a blink that comes out 20% slow
leaves you unable to tell a wrong model from an untrimmed oscillator.

Operating range is 0–28 MHz on this part, against the STC12's 35 MHz — so 33.1776 MHz is out of
spec here even though the RC can produce it.

## 6. What is unverified, and the opportunity

Everything above is read from a datasheet and a header. **Nothing in this document has been
confirmed on silicon** — the same standing caveat as the STC12 ADC section.

The part exists here — one STC15F2K60S2 — but is **not available for bench work at present**, so
treat every claim above as datasheet-derived until that changes. One chip is also a reason for
care rather than confidence: there is no second part to compare against if one is damaged, and
this is a 4.2 V minimum part on a 5 V nominal rail.

The verification order that buys the most per bench hour, for when it is available:

1. `stcgal -t 11059` and a blink — proves ISP, the trimmed RC, and Timer 0 at FOSC/12 in one go.
2. `src/02-adc` ported — the ADC register sequence has never been confirmed on *any* part, and
   since the ADC core registers are identical (§1), **confirming it on an STC15 is meaningful
   evidence for the STC12 too**, with §2.1's `ADRJ` caveat.
3. `src/10-live-firmware` — the baud path is already fixed (§4 above), so this is ready to
   flash as-is with `make PART=stc15f2k60s2 EXAMPLE=10-live-firmware`. Verified under ucsim:
   both builds produce AUXR=0x15 and the correct reload. The first real test of the debug
   monitor on silicon.

That third one would make the STC15, not the STC12, the first part in this project with a
bench-verified peripheral claim.
