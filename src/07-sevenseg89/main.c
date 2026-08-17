// 07-sevenseg89 — settle the A2 7-seg dispute by experiment. Our own
// documents disagree: the ROADMAP A2 block says common-ANODE, the
// vendor's module table says 共阴 (common CATHODE, 74HC245-driven).
// One firmware, two phases, an eyeball verdict:
//
//   phase A: P0 = 0x3F  -> a "0" pattern IF common-cathode (segment
//                          high = lit through the 245)
//   phase B: P0 = 0xC0  -> the complement: "0" IF common-anode
//
// Each phase walks the 74HC138 select (family convention: A/B/C on
// P2.2/P2.3/P2.4, so P2 = digit << 2) across all 8 digits, then the
// UART announces the phase switch. Whichever phase shows zeros
// marching across the tubes names the polarity; if NEITHER lights,
// the select convention is wrong too and the next revision sweeps
// other pins. The marquee LEDs are left alone.

#include <8051.h>

#define TICK_50MS (65536u - 46080u)

static void delay_50ms_ticks(unsigned char n)
{
    TMOD = (TMOD & 0xF0) | 0x01;
    while (n--) {
        TH0 = TICK_50MS >> 8;
        TL0 = TICK_50MS & 0xFF;
        TF0 = 0;
        TR0 = 1;
        while (!TF0)
            ;
        TR0 = 0;
    }
}

static void putch(char c)
{
    SBUF = c;
    while (!TI)
        ;
    TI = 0;
}

static void putstr(const char *s)
{
    while (*s)
        putch(*s++);
}

void main(void)
{
    unsigned char digit;

    SCON = 0x50;
    TMOD = (TMOD & 0x0F) | 0x20;
    TH1 = 0xFD;
    TL1 = 0xFD;
    TR1 = 1;

    for (;;) {
        putstr("phase A: P0=0x3F - zeros mean COMMON CATHODE\r\n");
        for (digit = 0; digit < 8; digit++) {
            P2 = digit << 2;            /* 138 A/B/C on P2.2..P2.4 */
            P0 = 0x3F;                  /* segments a-f high */
            delay_50ms_ticks(10);       /* 500 ms per digit */
        }
        P0 = 0x00;
        putstr("phase B: P0=0xC0 - zeros mean COMMON ANODE\r\n");
        for (digit = 0; digit < 8; digit++) {
            P2 = digit << 2;
            P0 = 0xC0;                  /* complement of 0x3F */
            delay_50ms_ticks(10);
        }
        P0 = 0xFF;
    }
}
