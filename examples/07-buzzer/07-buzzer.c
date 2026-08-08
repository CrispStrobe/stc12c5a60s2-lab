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

/* Tone on buzzer. Timer 1 in mode 1 toggles the pin, so the
 * frequency is FOSC/24/(65536 - reload) and the whole audible band is
 * reachable -- roughly 7 Hz upward. The hardware clock outputs (T1CLKO
 * and friends) divide an 8-BIT reload and bottom out at 1800 Hz, which
 * is a beeper rather than a tone; and clocking the PCA from Timer 0 gives
 * 3.9 Hz, because Timer 0 is already the millisecond tick. See
 * STC12-PERIPHERAL-MODEL.md 5b.
 *
 * This costs Timer 1 outright. The debug monitor wants it too, as the
 * wall clock behind skew_ms, so a program with a tone cannot also be run
 * under the monitor. */
#define BW_TONE_NUM (FOSC_HZ / 24UL)

static unsigned char bw_tone_h, bw_tone_l;

static void bw_tone_isr(void) __interrupt(3)
{
    TH1 = bw_tone_h;               /* mode 1 is not auto-reload */
    TL1 = bw_tone_l;
    P3_5 = !P3_5;
}

static void tone_set(unsigned long hz)
{
    unsigned long div;
    if (hz == 0) {                 /* silence, and park the pin */
        TR1 = 0;
        ET1 = 0;
        P3_5 = 0;
        return;
    }
    div = (BW_TONE_NUM + (hz >> 1)) / hz;   /* round, do not truncate: */
                                       /* truncating puts 1000 Hz at */
                                       /* 1001.7 rather than 999.6   */
    if (div < 1UL)     div = 1UL;
    if (div > 65535UL) div = 65535UL;
    div = 65536UL - div;
    bw_tone_h = (unsigned char)(div >> 8);
    bw_tone_l = (unsigned char)(div & 0xFF);
    TH1 = bw_tone_h;
    TL1 = bw_tone_l;
    ET1 = 1;
    TR1 = 1;
}

void main(void)
{

    AUXR &= ~0xC0;                 /* Timer 0 AND Timer 1 at FOSC/12 */
    TMOD  = (TMOD & 0xF0) | 0x01;  /* Timer 0, mode 1 */
    TMOD  = (TMOD & 0x0F) | 0x10;  /* Timer 1, mode 1: the tone */
    PT1   = 1;                     /* the tone outranks the tick:
                                    * jitter here is audible */
    tone_set(0);                   /* silent until asked */

    for (;;) {
        while (!(!P3_2)) ;
        tone_set(440);
        delay_ms(200);
        tone_set(554);
        delay_ms(200);
        tone_set(659);
        delay_ms(400);
        tone_set(0);
        while (!(!(!P3_2))) ;
    }
}
