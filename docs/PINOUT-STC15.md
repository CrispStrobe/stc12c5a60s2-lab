# STC15F2K60S2 — PDIP-40 pinout

Companion to [`PINOUT.md`](PINOUT.md), which covers the STC12C5A60S2. Register-level
differences are in [`STC15-PERIPHERAL-MODEL.md`](STC15-PERIPHERAL-MODEL.md); this file is about
where the legs go.

Source: [STC15 series datasheet](https://www.stcmicro.com/datasheet/STC15F2K60S2-en.pdf) §1.1.3
(p. 19). **Not verified against a physical part.**

> [!CAUTION]
> **This part is NOT pin-compatible with the STC12C5A60S2.** Not approximately, not mostly —
> the supply pin moves. On the STC12, VCC is pin 40 and pin 18 is XTAL2. On the STC15F2K60S2,
> **VCC is pin 18** and pin 40 is `P4.5/ALE`.
>
> Putting an STC15F2K60S2 into a socket wired for an STC12 applies the supply rail to a GPIO
> pin and drives the crystal pins from whatever the old circuit had there. Assume the chip is
> destroyed.
>
> The "drop-in socket" story in [README §8.1](../README.md) is about **STC12 ↔ STC89**, which
> share the classic 8051 pinout. The STC15 does not. Rewire the board.

## PDIP-40

```
                      ┌─────────∪──────────┐
            AD0/P0.0 ─│  1              40 │─ P4.5/ALE
            AD1/P0.1 ─│  2              39 │─ P2.7/A15/CCP2_3
            AD2/P0.2 ─│  3              38 │─ P2.6/A14/CCP1_3
            AD3/P0.3 ─│  4              37 │─ P2.5/A13/CCP0_3
            AD4/P0.4 ─│  5              36 │─ P2.4/A12/ECI_3/SS_2
            AD5/P0.5 ─│  6              35 │─ P2.3/A11/MOSI_2
            AD6/P0.6 ─│  7              34 │─ P2.2/A10/MISO_2
            AD7/P0.7 ─│  8    STC15     33 │─ P2.1/A9/SCLK_2
  RxD2/CCP1/ADC0/P1.0 │  9   F2K60S2    32 │─ P2.0/A8/RSTOUT_LOW
  TxD2/CCP0/ADC1/P1.1 │ 10              31 │─ P4.4/RD
      ECI/SS/ADC2/P1.2│ 11              30 │─ P4.2/WR
        MOSI/ADC3/P1.3│ 12              29 │─ P4.1/MISO_3
        MISO/ADC4/P1.4│ 13              28 │─ P3.7/INT3/TxD_2/CCP2_2
        SCLK/ADC5/P1.5│ 14              27 │─ P3.6/INT2/RxD_2/CCP1_2
 XTAL2/RxD_3/ADC6/P1.6│ 15              26 │─ P3.5/T1/T0CLKO/CCP0_2
 XTAL1/TxD_3/ADC7/P1.7│ 16              25 │─ P3.4/T0/T1CLKO/ECI_2
    SS_3/MCLKO/RST/P5.4│17              24 │─ P3.3/INT1
                  VCC ─│ 18              23 │─ P3.2/INT0
                 P5.5 ─│ 19              22 │─ P3.1/TxD/T2
                  GND ─│ 20              21 │─ P3.0/RxD/INT4/T2CLKO
                      └────────────────────┘
```

38 I/O pins in this package. Pin 1 is at the notched end.

## The four differences that will bite you

| | STC12C5A60S2 | STC15F2K60S2 |
|---|---|---|
| **VCC** | pin 40 | **pin 18** |
| **GND** | pin 20 | pin 20 *(the only one that matches)* |
| **RST** | pin 9, dedicated | **pin 17**, shared: `SS_3/MCLKO/RST/P5.4` |
| **P0** | pins 32–39, **descending** (32 = P0.7) | pins 1–8, **ascending** (1 = P0.0) |
| **P1** | pins 1–8 | pins 9–16 |
| **XTAL2 / XTAL1** | pins 18 / 19, dedicated | **pins 15 / 16**, shared with `P1.6`/`P1.7` |
| **UART1 RxD/TxD** | pins 10 / 11 | **pins 21 / 22** |

### A crystal costs you two ADC channels

`XTAL1`/`XTAL2` are multiplexed onto `P1.6`/`P1.7`, which are also `ADC7`/`ADC6`. Fitting a
crystal takes both away. On the STC12 the crystal pins are dedicated and cost nothing.

This matters less than it sounds, because on this part **you should not fit a crystal at all**:
the internal RC is ±0.3% and trimmable to 11.0592 MHz with `stcgal -t 11059`
(`STC15-PERIPHERAL-MODEL.md` §5). The datasheet says so itself — "No need external crystal and
reset". Leave P1.6/P1.7 as ADC inputs.

### UART1 can move, and the datasheet suggests moving it

`P3.0`/`P3.1` (pins 21/22) is the reset default and **the ISP bootloader pin pair**, exactly as
on the STC12. But UART1 can be remapped via `P_SW1` (0xA2) to `P3.6`/`P3.7` (pins 27/28) or
`P1.6`/`P1.7` (pins 15/16) — and the datasheet's own pin-configuration page recommends the
remap for the smaller packages.

**For `src/10-live-firmware` this is a genuine opportunity.** On the STC12, the debug monitor and
the ISP bootloader contend for the same pins, so you cannot have a terminal open while flashing
(`DEBUG-CONTROL-MODEL.md` §9). On an STC15 the monitor could sit on the remapped pins and leave
`P3.0`/`P3.1` free for ISP. ⚠ Untested, and it costs an ADC channel pair or two GPIOs.

## Reset

**Active high, same as the STC12** — a plain 1 kΩ to GND is the whole circuit below 12 MHz, and
the 10 kΩ + 10 µF network from active-low 8051 schematics is still wrong here.

The difference is that RST is not a dedicated pin: it is `P5.4`, and it is also `MCLKO` (master
clock output) and `SS_3`. Out of reset it is the reset input; making it GPIO is an ISP option,
and `stcgal`'s `reset_pin_enabled` is the bit that controls it. **Do not clear that option on
your only chip** — with the reset pin disabled and no crystal, recovery gets awkward.

`RSTOUT_LOW` on pin 32 (`P2.0`) is an output the MCU drives low during reset, for holding
peripherals in reset. It has no STC12 equivalent.

## Supply

**5.5 – 4.2 V** for `STC15F2K60S2`. The 3.3 V sibling is `STC15L2K60S2` at 3.6 – 2.4 V, and 5 V
destroys it — check the marking.

Note the **4.2 V minimum**, which is tighter than the STC12's 3.5 V. A USB rail sagging under
LED load that an STC12 shrugs off can drop this part below spec. Measure at the chip, not at the
adapter.

## What is not in this package

`P0`–`P3` are complete. `P4` gives you only `P4.1`, `P4.2`, `P4.4`, `P4.5`; `P5` gives you only
`P5.4` and `P5.5`. There is no `P6`/`P7` on this part in any package ⚠ (absent from the §1.1.1
feature list; this pinout confirms it for PDIP-40).

Also absent, as on the STC12: **no `EA` pin and no `PSEN`** — the chip always runs from internal
flash, which is why the Keil Monitor-51 approach is unbuildable here too
([`DEBUG-CONTROL-MODEL.md`](DEBUG-CONTROL-MODEL.md) §5).

## Other packages

LQFP-44 (42 I/O), LQFP-32 (30 I/O), SOP-28/SKDIP-28 (26 I/O), TSSOP-20 (18 I/O). Datasheet
§1.1.3 p. 19–20 has all of them; this repo assumes PDIP-40 throughout.
