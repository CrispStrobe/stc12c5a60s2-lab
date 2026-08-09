/* Generated from BrickWright pseudocode by stc-compiler.
 * Hand edits will be lost; change the pseudocode instead. */
#include <stc12.h>

#define FOSC_HZ 11059200UL

/* Timer 0, mode 1, clocked at FOSC/12 -- accuracy depends only on
 * FOSC, and every supported family counts this mode identically, so
 * the same program is timing-correct on a 12T STC89 and a 1T STC12
 * or STC15. Nothing in the generated code ever busy-waits. */
#define T0_RELOAD (65536UL - (FOSC_HZ / 12UL / 1000UL))

static void delay_ms(unsigned int ms)
{
    while (ms--) {
        TL0 = (unsigned char)(T0_RELOAD & 0xFF);
        TH0 = (unsigned char)(T0_RELOAD >> 8);
        TF0 = 0;
        TR0 = 1;
        while (!TF0) ;
        TR0 = 0;
        TF0 = 0;
    }
}

/* Lookup tables live in code space: flash is the abundant resource
 * here and RAM is not. `const __code` keeps them out of the 256 bytes
 * that matter. */
static const __code unsigned char bw_tab_font[] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71 };

/* A computed index is clamped rather than trusted. Reading past a
 * table means reading a random byte of flash and, on a display,
 * showing it -- which looks like data rather than like a fault. A
 * constant index is checked at compile time and costs nothing. */
static unsigned char bw_clamp(int i, unsigned char last)
{
    if (i < 0) return 0;
    if (i > (int)last) return last;
    return (unsigned char)i;
}

/* Variables (16-bit signed, like Scratch's integers). */
static int digit = 0;

void main(void)
{
    P0M1 &= ~0xFF;   /* push-pull */
    P0M0 |=  0xFF;

    AUXR &= ~0x80;                 /* Timer 0 at FOSC/12 */
    TMOD  = (TMOD & 0xF0) | 0x01;  /* Timer 0, mode 1 */

    digit = 0;
    for (;;) {
        P0 = (unsigned char)(bw_tab_font[bw_clamp(digit, 15)]);
        delay_ms(500);
        digit += 1;
        if (digit > 15) {
            digit = 0;
        }
    }
}
