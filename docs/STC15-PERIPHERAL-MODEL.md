# The STC15 peripheral model — a delta against the STC12, not a second document

**Why a delta.** [`STC12-PERIPHERAL-MODEL.md`](STC12-PERIPHERAL-MODEL.md) already describes a
1T STC core with `PxM1`/`PxM0` port modes, a Timer 0 at FOSC/12, and a 10-bit ADC on P1. The
STC15F2K60S2 is the same picture with pieces added and a small number of pieces moved. Copying
the STC12 document and editing it would produce two texts that drift; this one says only what is
different, and everything it does not mention is unchanged and governed by that document.

Same rule as its parent: **implementations cite this, they do not re-derive it.** The same three
implementations are in scope — the ucsim fork, the emu8051 fork, and the on-chip monitor.

**Part in scope: `STC15F2K60S2`.** The STC15 family is wide (STC15W4K32S4, STC15W408AS,
STC15F101W …) and the variants genuinely differ — a third PCA module, the enhanced PWM block and
the extra UARTs are not on every part. Where a register below is family-wide rather than
confirmed for this part, it is marked ⚠.

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

| block | registers |
|---|---|
| **Timer 2** | `T2H` 0xD6, `T2L` 0xD7 — the UART1 baud source, and a general 16-bit auto-reload timer |
| **Timers 3 and 4** ⚠ | `T4T3M` 0xD1, `T4H` 0xD2, `T4L` 0xD3, `T3H` 0xD4, `T3L` 0xD5 |
| **UART 3 and 4** ⚠ | `S3CON` 0xAC, `S3BUF` 0xAD, `S4CON` 0x84, `S4BUF` 0x85 |
| **SPI** ⚠ | `SPSTAT` 0xCD, `SPCTL` 0xCE, `SPDAT` 0xCF |
| **IAP / EEPROM** | `IAP_DATA` 0xC2, `IAP_ADDRH` 0xC3, `IAP_ADDRL` 0xC4, `IAP_CMD` 0xC5, `IAP_TRIG` 0xC6, `IAP_CONTR` 0xC7, `WDT_CONTR` 0xC1 |
| **Ports 6 and 7** ⚠ | `P6` 0xE8, `P6M1` 0xCB, `P6M0` 0xCC, `P7M1` 0xE1, `P7M0` 0xE2, `P7` 0xF8 |
| **Third PCA module** ⚠ | `CCAPM2` 0xDC, `CCAP2L` 0xEC, `CCAP2H` 0xFC, `PCA_PWM2` 0xF4 |
| **Enhanced PWM** ⚠ | `PWMCFG` 0xF1, `PWMCR` 0xF5, `PWMIF` 0xF6, `PWMFDCR` 0xF7 — STC15W4KxxS4 only per the header; confirm for this part |
| **Wake-up timer** | `WKTCL` 0xAA, `WKTCH` 0xAB |
| **Peripheral switch 2** | `P_SW2` 0xBA |
| **Bus speed** | `BUS_SPEED` 0xA1 |

Gone relative to the STC12: **`P4SW` (0xBB) is not present**, and this part exposes no P4 mode
registers in the same arrangement ⚠.

## 4. Consequences for what this repo has already built

- **`stc_pseudocode.py` is correct as it stands.** Its `PARTS` entry maps `stc15f2k60s2` to
  `stc12.h`, on the stated grounds that every register the emitter touches is at the same
  address. That claim now has a second source behind it: P0–P3, `PxM0`/`PxM1`, `AUXR.T0x12`,
  Timer 0, `P1ASF` and the ADC block are all confirmed identical. The comment's list of "famous
  divergences it never writes" was right, and `ADRJ` (§2.1) can be added to it.
- **`src/10-live-firmware` will NOT work on an STC15 as written**, and §2.2 says exactly why: it
  writes an 8-bit reload to `BRT` (0x9C), where an STC15 needs a 16-bit reload in `T2H`/`T2L`.
  The mode bits in `AUXR` happen to be right. Everything else in the monitor — Level 1 position,
  the yield-point matching, the curated SFR window — is family-agnostic. Fixing this is one
  `#if`, and it is the natural first bench task now that STC15 silicon exists.
- **The curated SFR window in `include/live-sfr.h` is STC12's.** An STC15 build needs the §3
  additions and must drop `P4SW`.

## 5. The one genuine advantage: the clock stops being a guess

The parent document notes that the STC12's internal RC is 11–17 MHz and drifts, which is why
`FOSC` needs tuning by eye and why a crystal is recommended. **The STC15 lets the ISP tool set
the internal RC frequency at download time** — `stcgal -t <kHz>`, documented as "RC oscillator
frequency in kHz (STC15+ series only)".

That removes the single most annoying unknown in the STC12 workflow. A `WAIT 1 SECOND` can be
expected to take a second without a crystal fitted, which also makes the STC15 the better part
for *verifying* timing claims.

## 6. What is unverified, and the opportunity

Everything above is read from a datasheet and a header. **Nothing in this document has been
confirmed on silicon** — the same standing caveat as the STC12 ADC section.

The difference is that this time **there is hardware**. The verification order that buys the most
per bench hour:

1. `stcgal -t 11059` and a blink — proves ISP, the trimmed RC, and Timer 0 at FOSC/12 in one go.
2. `src/02-adc` ported — the ADC register sequence has never been confirmed on *any* part, and
   since the ADC core registers are identical (§1), **confirming it on an STC15 is meaningful
   evidence for the STC12 too**, with §2.1's `ADRJ` caveat.
3. `src/10-live-firmware` with the §2.2 baud fix — the first real test of the debug monitor.

That third one would make the STC15, not the STC12, the first part in this project with a
bench-verified peripheral claim.
