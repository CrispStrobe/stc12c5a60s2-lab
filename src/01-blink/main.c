/*
 * 01-blink — the smoke test for a fresh STC12C5A60S2.
 *
 * Alternates two LEDs, then blinks them together twice, forever. Three
 * things are being proved at once:
 *
 *   1. the chip took the flash (stcgal talked to the bootloader),
 *   2. it is running your code out of reset,
 *   3. FOSC_HZ is roughly right — the "together" phase is timed to be one
 *      second on, one second off, so you can check it against a clock.
 *
 * SPDX-License-Identifier: MIT
 */
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
            LED1 = LED_ON;
            LED2 = LED_OFF;
            delay_ms(150);

            LED1 = LED_OFF;
            LED2 = LED_ON;
            delay_ms(150);
        }

        /* Phase 2: both together, 1 s on / 1 s off, twice. */
        for (i = 0; i < 2; i++) {
            LED1 = LED_ON;
            LED2 = LED_ON;
            delay_ms(1000);

            LED1 = LED_OFF;
            LED2 = LED_OFF;
            delay_ms(1000);
        }
    }
}
