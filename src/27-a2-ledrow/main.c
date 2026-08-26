// Prechin A2 D1..D8 row polarity/mapping test for STC89C52RC.
// Remove LCD1602; leave J24 at OE-VCC to disable the 8x8 matrix.

#include <8051.h>

#define TIMER0_1MS (65536u - 922u)

static void delay_ms(unsigned int ms)
{
    while (ms--) {
        TH0 = TIMER0_1MS >> 8;
        TL0 = TIMER0_1MS & 0xff;
        TF0 = 0;
        TR0 = 1;
        while (!TF0)
            ;
        TR0 = 0;
    }
}

void main(void)
{
    unsigned char bit;

    TMOD = (TMOD & 0xf0) | 0x01;
    P0 = 0x00;                  // blank common-cathode seven-segment segments

    for (;;) {
        P2 = 0xff;              // expected all off if LEDs are active low
        delay_ms(2000);
        P2 = 0x00;              // expected all on if LEDs are active low
        delay_ms(2000);

        // Active-low walking dot, P2.0 through P2.7.
        for (bit = 0; bit < 8; ++bit) {
            P2 = (unsigned char)~(1u << bit);
            delay_ms(500);
        }
        P2 = 0xff;
        delay_ms(1500);
    }
}
