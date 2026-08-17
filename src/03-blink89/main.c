// 03-blink89 — first-contact blink for the STC89C52RC dev board.
//
// Deliberately self-contained: no board.h (that pin map and its
// P1M1/P1M0 port-mode setup are STC12-only — the STC89 has no port-mode
// registers, every pin is quasi-bidirectional, full stop). The dev
// board's 8-LED marquee sits on one of P0/P1/P2 depending on jumpers,
// so toggle all three: wherever the LEDs hang, they blink. LEDs on
// these boards are active-low (sink current — same §4.6 guidance as the
// STC12). P3 is left alone: P3.0/P3.1 are the CH340 serial bridge and
// the ISP path.
//
// Timing is Timer 0, polled, mode 1: at 11.0592 MHz / 12T that is
// 921 600 machine cycles per second, so 46 080 counts = 50 ms exactly.
// Ten ticks per toggle = 1 Hz blink, crystal-accurate — a stopwatch on
// the LED verifies FOSC, which is the second thing this program proves.

#include <8051.h>

#define TICK_50MS (65536u - 46080u)

static void delay_50ms_ticks(unsigned char n)
{
    TMOD = (TMOD & 0xF0) | 0x01;    // Timer 0, mode 1 (16-bit)
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

void main(void)
{
    for (;;) {
        P0 = 0x00;                  // LEDs on (active low)
        P1 = 0x00;
        P2 = 0x00;
        delay_50ms_ticks(10);       // 500 ms
        P0 = 0xFF;                  // LEDs off
        P1 = 0xFF;
        P2 = 0xFF;
        delay_50ms_ticks(10);
    }
}
