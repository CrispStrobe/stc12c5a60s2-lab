// 08-ledfind89 — locate the A2's 8-LED module by elimination. P1 is
// the measured keypad, so the marquee must hang off P0, P2 or P3.
// Three phases, five seconds each, 2 Hz toggle, forever:
//
//   phase 1: P0 toggles   (7-seg segments will flicker — expected)
//   phase 2: P2 toggles   (7-seg digit select dances — expected)
//   phase 3: P3.2..P3.7 toggle (UART pins untouched)
//
// The observer reports which phase blinks the LED module; the UART
// announces each phase for the host-side log. Polarity falls out for
// free: if the LEDs are lit in the "low" half-second, they're active
// low.

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

static void toggle_phase(unsigned char which)
{
    unsigned char i;
    for (i = 0; i < 10; i++) {          /* 10 half-seconds = 5 s */
        unsigned char low = i & 1;
        switch (which) {
        case 0: P0 = low ? 0x00 : 0xFF; break;
        case 1: P2 = low ? 0x00 : 0xFF; break;
        default:
            P3 = low ? 0x03 : 0xFF;     /* keep P3.0/P3.1 high */
            break;
        }
        delay_50ms_ticks(10);
    }
    P0 = 0xFF;
    P2 = 0xFF;
    P3 = 0xFF;
}

void main(void)
{
    SCON = 0x50;
    TMOD = (TMOD & 0x0F) | 0x20;
    TH1 = 0xFD;
    TL1 = 0xFD;
    TR1 = 1;

    for (;;) {
        putstr("phase 1: P0\r\n");
        toggle_phase(0);
        putstr("phase 2: P2\r\n");
        toggle_phase(1);
        putstr("phase 3: P3\r\n");
        toggle_phase(2);
    }
}
