/*
 * delay.h — millisecond delay built on Timer 0.
 *
 * A busy-wait `for` loop would work too, but its timing depends on how
 * SDCC happens to compile it. Timer 0 counts real clock edges, so the
 * only thing that can be wrong here is FOSC_HZ itself.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef DELAY_H
#define DELAY_H

#include "board.h"

/*
 * The STC12 is a 1T core: it can clock its timers straight off FOSC. We
 * deliberately keep the traditional 8051 divide-by-12 (AUXR.T0x12 = 0) so
 * the reload value stays small enough for a 1 ms tick at any sane FOSC.
 */
#define T0_TICKS_PER_MS (FOSC_HZ / 12UL / 1000UL)
#define T0_RELOAD       (65536UL - T0_TICKS_PER_MS)

static void delay_init(void)
{
#ifndef PART_STC89C52RC
    AUXR &= ~0x80;              /* T0x12 = 0 -> Timer 0 runs at FOSC/12 */
#endif
    /* The STC89's 12T core has no T0x12 bit - Timer 0 is FOSC/12 by
     * construction, which is exactly the rate every delay here assumes. */
    TMOD  = (TMOD & 0xF0) | 0x01;  /* Timer 0, mode 1: 16-bit */
    TR0   = 0;
    TF0   = 0;
}

static void delay_ms(unsigned int ms)
{
    while (ms--) {
        TL0 = (unsigned char)(T0_RELOAD & 0xFF);
        TH0 = (unsigned char)((T0_RELOAD >> 8) & 0xFF);
        TF0 = 0;
        TR0 = 1;
        while (!TF0)
            ;                   /* wait for the 16-bit counter to overflow */
        TR0 = 0;
        TF0 = 0;
    }
}

#endif /* DELAY_H */
