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

/* leds: a 74HC595, eight outputs for three pins.
 *
 * Admitted to the parts library because its correctness depends on the
 * ORDER of the edges and not on their duration: the part is specified
 * into the tens of megahertz and has no minimum clock period an 8051
 * could violate, so there is no delay here to get wrong. Data is sampled
 * on the rising edge of the shift clock, and the latch transfers on its
 * own rising edge. docs/PARTS-MODEL.md.
 *
 * MSB first, so the byte reads left to right on the outputs. */
static void bw_part_leds(unsigned char value)
{
    unsigned char i;
    P3_6 = 0;
    P3_5 = 0;
    for (i = 0; i < 8; i++) {
        P3_4 = (value & 0x80) ? 1 : 0;
        value = (unsigned char)(value << 1);
        P3_6 = 1;
        P3_6 = 0;
    }
    P3_5 = 1;      /* transfer to the outputs */
    P3_5 = 0;
}

/* Lookup tables live in code space: flash is the abundant resource
 * here and RAM is not. `const __code` keeps them out of the 256 bytes
 * that matter. */
static const __code unsigned char bw_tab_walk[] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };

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
static int step = 0;

void main(void)
{
    P3M1 &= ~0x70;   /* push-pull */
    P3M0 |=  0x70;

    AUXR &= ~0x80;                 /* Timer 0 at FOSC/12 */
    TMOD  = (TMOD & 0xF0) | 0x01;  /* Timer 0, mode 1 */

    step = 0;
    for (;;) {
        bw_part_leds((unsigned char)~(bw_tab_walk[bw_clamp(step, 7)]));
        delay_ms(120);
        step += 1;
        if (step > 7) {
            step = 0;
        }
    }
}
