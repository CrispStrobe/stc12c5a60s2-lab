/*
 * 02-adc — the hardware check for the ADC path.
 *
 * A potentiometer on P1.3 sets the blink rate of LED1: fully one way is a
 * lazy one-second blink, fully the other way a 60 ms flicker. The point is
 * not the blinking — it is that the ADC register sequence in this file was
 * written from the datasheet (sections 10.x) and has never been confirmed
 * on silicon. If turning the pot changes the rate smoothly across the whole
 * travel, the sequence is right: P1ASF really routed the pin to the ADC,
 * the start/flag bits are where the datasheet says, and the 10-bit result
 * assembles in the right order.
 *
 * Wiring:  pot ends to +5 V and GND, wiper to P1.3 (PDIP-40 pin 4).
 *          LED1 as in board.h (P1.0, active low).
 *
 * SPDX-License-Identifier: MIT
 */
#include "board.h"
#include "delay.h"

#define ADC_CHANNEL 3          /* wiper on P1.3 = ADC3 */

/* ADC_CONTR bits, datasheet 10.3. */
#define ADC_POWER   0x80
#define ADC_START   0x08
#define ADC_FLAG    0x10

static void adc_init(void)
{
    /* Route P1.3 to the ADC and make it a true high-impedance input;
     * an analog pin must not fight the quasi-bidirectional pull-ups. */
    P1ASF |= (1 << ADC_CHANNEL);
    P1M1  |= (1 << ADC_CHANNEL);
    P1M0  &= ~(1 << ADC_CHANNEL);

    ADC_CONTR = ADC_POWER;      /* power the block, then let it settle */
    delay_ms(2);                /* datasheet asks for >= 1 ms after power-up */
}

static unsigned int adc_read(unsigned char channel)
{
    unsigned char settle;

    ADC_CONTR = ADC_POWER | ADC_START | channel;
    for (settle = 0; settle < 8; settle++) ;   /* mux settle, a few us */
    while (!(ADC_CONTR & ADC_FLAG)) ;          /* conversion done? */
    ADC_CONTR &= ~ADC_FLAG;                    /* flag clears by hand */

    /* AUXR1.2 (ADRJ) is 0 out of reset: high 8 bits in ADC_RES, low 2 in
     * ADC_RESL. 0..1023. */
    return ((unsigned int)ADC_RES << 2) | (ADC_RESL & 0x03);
}

void main(void)
{
    unsigned int value;

    board_init();
    delay_init();
    adc_init();

    for (;;) {
        value = adc_read(ADC_CHANNEL);

        /* 0..1023 -> 30..541 ms per half-period, so the change is visible
         * across the whole pot travel. */
        LED1 = LED_ON;
        delay_ms(30 + (value >> 1));
        LED1 = LED_OFF;
        delay_ms(30 + (value >> 1));
    }
}
