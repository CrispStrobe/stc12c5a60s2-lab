/*
 * 4x4x4 bi-colour LED cube — the mapping probe.
 *
 * The kit (ICStation 4682 / HackerBox 005) multiplexes 64 LEDs as 8 select
 * lines on P2, active low, against 8 data bits on P0. That much is measured,
 * not assumed: rgm3/ledcube444's firmware was run under emu8051-stc and P2 was
 * seen cycling exactly {FE,FD,FB,F7,EF,DF,BF,7F} at 1.235 ms a step, so a whole
 * frame is 9.88 ms and the cube refreshes at 101 Hz.
 *
 * What is NOT known is which physical voxel each (select, bit) pair lights.
 * Sixty-four bits and sixty-four positions in a 4x4x4, but the kit is sold as
 * bi-colour, which would want a hundred and twenty-eight — so either a colour
 * costs a select line, or half the cube is one colour. Reading the vendor's
 * animation tables cannot settle it: they were authored by looking at a cube.
 *
 * So this walks the 64 bits one at a time, slowly, in a known order, and the
 * answer is written down by watching the thing. Flash it, film it, fill in the
 * table in README.md. That is the only way this gets to be a fact rather than
 * a guess, and everything above it -- the simulator's cube, the blocks that
 * author animations -- is wrong in the same way if the map is wrong.
 *
 * Ours, MIT, written against the measured scan model rather than derived from
 * the vendor package.
 */
#include <stc12.h>
#include <stdint.h>

#define FOSC_HZ 11059200UL
#define T0_RELOAD (65536UL - (FOSC_HZ / 12UL / 1000UL))

/* Active-low select lines, in the order the hardware expects. */
__code static uint8_t SELECT[8] = {0xFE, 0xFD, 0xFB, 0xF7, 0xEF, 0xDF, 0xBF, 0x7F};

/* Timer 0 at FOSC/12: the one mode a 1T STC12 and a 12T STC89 count the same.
 * The vendor firmware uses a software delay loop instead, which is why its
 * animation speed is ~8x off on this 1T part (README.md, "the 1T trap"). */
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

/* One multiplex pass over an 8-byte frame. Blank before switching the select,
 * or the previous step's data ghosts onto this one's LEDs. */
static void show(uint8_t *frame, unsigned int repeats)
{
    unsigned char i;
    while (repeats--) {
        for (i = 0; i < 8; i++) {
            P0 = 0x00;
            P2 = SELECT[i];
            P0 = frame[i];
            delay_ms(1);
        }
    }
}

void main(void)
{
    unsigned char sel, bit, i;
    uint8_t blank[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t frame[8];

    P0M0 = 0xFF;                       /* push-pull: the cube sinks real current */
    P0M1 = 0x00;
    TMOD = (TMOD & 0xF0) | 0x01;       /* Timer 0, mode 1 */
    AUXR &= ~0x80;                     /* and at FOSC/12 */

    for (;;) {
        for (sel = 0; sel < 8; sel++) {
            for (bit = 0; bit < 8; bit++) {
                for (i = 0; i < 8; i++) frame[i] = 0;
                frame[sel] = (uint8_t)(1u << bit);
                /* ~600 ms per voxel: long enough to write down, short enough
                 * that a full pass takes under a minute. */
                show(frame, 60);
                show(blank, 6);        /* a gap, so two voxels never blur into one */
            }
        }
        show(blank, 200);              /* a longer gap between passes */
    }
}
