/*
 * board.h — board definition for the STC12C5A60S2 two-LED test rig.
 *
 * Everything hardware-specific lives here so the examples in src/ stay
 * readable. If you move an LED to another pin, change it here only.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef BOARD_H
#define BOARD_H

#include <stc12.h>

/*
 * The STC15F2K60S2 borrows stc12.h deliberately: 74 SFRs share both name and
 * address, and none keeps its name at a different address, so everything this
 * board file touches (P1, P1M0/P1M1) is identical. stc15-extra.h adds what is
 * genuinely new. See docs/STC15-PERIPHERAL-MODEL.md.
 */
#ifdef PART_STC15F2K60S2
#include "stc15-extra.h"
#endif

/* ------------------------------------------------------------------ clock
 * FOSC_HZ must match whatever actually clocks the chip, because every
 * delay in this repo is derived from it.
 *
 *   - External crystal fitted between XTAL1 (pin 19) and XTAL2 (pin 18):
 *     use its nominal frequency. 11.0592 MHz is the classic 8051 choice
 *     because it divides into exact UART baud rates.
 *   - No crystal fitted: the chip free-runs on its internal RC oscillator,
 *     which is only specified as 11-17 MHz at 5 V. Blink will still work,
 *     it will just be off by up to ~50%. Nudge FOSC_HZ until a "1000 ms"
 *     blink actually takes a second.
 *
 * Override without editing this file:  make FOSC=12000000
 */
#ifndef FOSC_HZ
#define FOSC_HZ 11059200UL
#endif

/* -------------------------------------------------------------------- LEDs
 * LED1 -> P1.0, PDIP-40 pin 1
 * LED2 -> P1.1, PDIP-40 pin 2
 *
 * Both are wired active-LOW (the MCU sinks the current):
 *
 *     +5V ---[ 1k ]---|>|--- P1.x
 *                     LED
 *
 * So writing 0 to the pin lights the LED. This is the wiring STC
 * recommends (datasheet 4.6) and it is the one arrangement that works in
 * every port mode, including the weak quasi-bidirectional mode the chip
 * powers up in.
 */
#define LED1    P1_0
#define LED2    P1_1

#define LED_ON  0
#define LED_OFF 1

/* Bit masks for the same two pins, for the port-mode registers below. */
#define LED_MASK 0x03   /* P1.0 | P1.1 */

/* ------------------------------------------------------- port mode helper
 * Each port has two mode registers, PxM1 and PxM0. Per bit:
 *
 *   M1 M0   mode
 *   -----   --------------------------------------------------------------
 *    0  0   quasi-bidirectional (power-on default) — sinks 20 mA, but
 *           sources only ~230 uA through a weak pull-up
 *    0  1   push-pull / strong pull-up — sources up to 20 mA
 *    1  0   input-only (high impedance, Schmitt trigger)
 *    1  1   open-drain — needs an external pull-up
 *
 * We put the LED pins in push-pull. The LEDs are sink-driven either way,
 * but push-pull gives a hard VCC on the "off" state instead of a weak
 * pull-up, so the LEDs go fully dark and switch crisply.
 */
static void board_init(void)
{
    P1M1 &= ~LED_MASK;   /* M1 = 0 */
    P1M0 |=  LED_MASK;   /* M0 = 1  -> push-pull */

    LED1 = LED_OFF;
    LED2 = LED_OFF;
}

#endif /* BOARD_H */
