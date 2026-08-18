// 13-hello115 — a chatterer at 115200 baud, for probing whether the
// host's serial stack can HEAR that rate at all (the stcbsl mystery:
// RX after set_baud(9600) works, 115200 never heard anything). The
// STC89's Timer 1 cannot make 115200 from 11.0592 MHz, but the 8052's
// Timer 2 as baud generator can, exactly: 11059200 / 32 / 3 = 115200,
// reload 65536-3 = 0xFFFD — the same arithmetic the BSL's own FF FD
// parameter uses, which is a nice symmetry.

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
    unsigned int n = 0;

    SCON = 0x50;
    RCAP2H = 0xFF;              /* T2 reload: 65536-3 -> 115200 exact */
    RCAP2L = 0xFD;
    T2CON = 0x34;               /* RCLK | TCLK | TR2: T2 is the baud clock */

    for (;;) {
        putstr("hello115 #");
        putch('0' + (n / 100) % 10);
        putch('0' + (n / 10) % 10);
        putch('0' + n % 10);
        putstr("\r\n");
        n++;
        P1 = (n & 1) ? 0x00 : 0xFF;     /* visible heartbeat */
        delay_50ms_ticks(10);
    }
}
