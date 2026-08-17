// 04-hello89 — two-way proof on the STC89C52RC: firmware prints over
// the CH340 and the host reads it back. No BRT on this part (that is
// STC12 hardware), so the baud clock is Timer 1 in mode 2 (8-bit
// auto-reload): 11.0592 MHz / 12T = 921 600 cycles/s, /32 with SMOD=0,
// reload 0xFD -> 9600 baud. The measured crystal (11.030 MHz per the
// BSL handshake) is 0.26% off nominal — well inside UART tolerance.
//
// P1 blinks in step with the printing so the LED proof and the serial
// proof are the same heartbeat. P3.0/P3.1 belong to the UART; P0/P2
// are left alone here (7-segment / LCD lines on the dev board).

#include <8051.h>

#define TICK_50MS (65536u - 46080u)

static void delay_50ms_ticks(unsigned char n)
{
    TMOD = (TMOD & 0xF0) | 0x01;    // Timer 0, mode 1 (Timer 1 bits kept)
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

static void putdec(unsigned int v)
{
    char buf[5];
    unsigned char i = 0;
    do {
        buf[i++] = '0' + v % 10;
        v /= 10;
    } while (v);
    while (i)
        putch(buf[--i]);
}

void main(void)
{
    unsigned int n = 0;

    SCON = 0x50;                    // UART mode 1, REN
    TMOD = (TMOD & 0x0F) | 0x20;    // Timer 1, mode 2 (8-bit auto-reload)
    TH1 = 0xFD;                     // 9600 baud @ 11.0592 MHz, 12T, SMOD=0
    TL1 = 0xFD;
    TR1 = 1;

    for (;;) {
        P1 = 0x00;                  // LEDs on (active low)
        putstr("STC89C52RC hello #");
        putdec(n++);
        putstr("\r\n");
        delay_50ms_ticks(10);       // 500 ms
        P1 = 0xFF;                  // LEDs off
        delay_50ms_ticks(10);
    }
}
