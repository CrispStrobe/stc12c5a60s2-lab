// 11-matrix89 v4 — the hunt, now self-labeling: the mode number shows
// on the 7-seg WHILE the matrix pattern drives, time-multiplexed over
// the shared P0 bus (matrix phase ~8 ms, digit phase ~2 ms — both look
// steady to the eye; the conflict IS the board's design). v3's finding,
// owner-measured: J24 is GND-OE-VCC and the shipped cap sat on OE-VCC —
// the 595 was output-DISABLED by a jumper through every correct-pin
// firmware. Cap belongs on the left pair (OE-GND).
// The 595 pins are now SILKSCREEN FACT (owner-read off the board):
// SER=P3.4, RCLK=P3.5, SCLK=P3.6 — which is exactly what v1 drove,
// and v1 was dark. So the unknown is the OTHER axis (P0 assumed, but
// maybe P2) or an analog gate (the "Led" trimmer near the matrix, or
// module seating). No scanning here: every mode drives ALL 64 LEDs
// steady for ~2.5 s so even a dim or partial path is visible.
//
//   mode 1: 595=0xFF P0=0x00   mode 2: 595=0xFF P0=0xFF
//   mode 3: 595=0x00 P0=0x00   mode 4: 595=0x00 P0=0xFF
//   mode 5: 595=0xFF P2=0x00   mode 6: 595=0xFF P2=0xFF
//   mode 7: 595=0x00 P2=0x00   mode 8: 595=0x00 P2=0xFF
//
// In modes 5-8 the LED module (measured on P2) lights too — expected;
// the question is whether the MATRIX does. The observer should also
// sweep the "Led" trimmer during the cycle. UART announces each mode.

#include <8051.h>

#define SER   P3_4
#define RCLK  P3_5
#define SRCLK P3_6

#define TICK_50MS (65536u - 46080u)

/* common-cathode digits 1..8 (a=bit0..g=bit6), from 09-keyshow89 */
static const unsigned char font[9] = {
    0x00, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F
};

static void dwell_ms(unsigned char ms)
{
    unsigned char i;
    while (ms--)
        for (i = 0; i < 230; i++)
            __asm__("nop");
}

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

static void shift595(unsigned char value)
{
    unsigned char i;
    RCLK = 0;
    for (i = 0; i < 8; i++) {
        SER = (value & 0x80) ? 1 : 0;
        value <<= 1;
        SRCLK = 1;
        SRCLK = 0;
    }
    RCLK = 1;
    RCLK = 0;
}

void main(void)
{
    unsigned char mode;

    SCON = 0x50;
    TMOD = (TMOD & 0x0F) | 0x20;
    TH1 = 0xFD;
    TL1 = 0xFD;
    TR1 = 1;

    for (;;) {
        for (mode = 1; mode <= 8; mode++) {
            putstr("mode ");
            putch('0' + mode);
            putstr("\r\n");
            {
                unsigned char sel = ((mode - 1) & 2) ? 0x00 : 0xFF;
                unsigned char rows = (mode & 1) ? 0x00 : 0xFF;
                unsigned char t;
                for (t = 0; t < 250; t++) {     /* ~2.5 s of 10 ms slots */
                    /* matrix phase: pattern on, tubes dark */
                    shift595(sel);
                    if (mode <= 4) {
                        P2 = 0xFF;
                        P0 = rows;
                    } else {
                        P0 = 0xFF;
                        P2 = rows;
                    }
                    dwell_ms(8);
                    /* digit phase: mode number on digit 0, matrix blanked */
                    shift595((unsigned char)~sel);
                    P2 = 0xE3;                  /* 138 digit 0, rest high */
                    P0 = font[mode];
                    dwell_ms(2);
                }
            }
            P0 = 0x00;
            P2 = 0xFF;
            shift595(0x00);
            delay_50ms_ticks(4);
        }
    }
}
