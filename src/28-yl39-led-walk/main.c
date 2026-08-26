// 28-yl39-led-walk — unmistakable second image for real-flash testing.
//
// The YL-39 LED row is active-low on P1. D1/D2 remain dark while a single
// light walks from D3 through D8, followed by two flashes of D3..D8 together.
// This is intentionally visually distinct from 01-blink's D1/D2 pattern.

#include <8052.h>
#include "delay.h"

#define UPPER_LEDS 0xfc

void main(void)
{
    unsigned char bit;
    unsigned char repeat;

    delay_init();
    P1 = 0xff;

    for (;;) {
        for (bit = 2; bit < 8; bit++) {
            P1 = (unsigned char)~(1u << bit);
            delay_ms(180);
        }
        P1 = 0xff;
        delay_ms(300);

        for (repeat = 0; repeat < 2; repeat++) {
            P1 = (unsigned char)~UPPER_LEDS;
            delay_ms(300);
            P1 = 0xff;
            delay_ms(300);
        }
        delay_ms(500);
    }
}
