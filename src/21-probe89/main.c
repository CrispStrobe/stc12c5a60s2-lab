// 21-probe89 — interactive bench probe for the "51 minimum system" board
// (see src/05-discover89/README.md for everything already measured).
//
// Two holes remain in that board map and this firmware closes both from
// the HOST side, so the owner only reports what they hear/see:
//
//   b<p><b>  buzz  P<p>.<b>: ~1 kHz square wave for ~2 s   (buzzer hunt)
//   d<p><b>  digit probe P<p>.<b>: P0 = 0x00 (all segments driven low,
//            the common-anode ON level measured on 2026-08-17), then the
//            select pin HIGH ~1.5 s, then LOW ~1.5 s. Whichever phase
//            lights a digit tells us the select's active level; WHICH
//            digit lights maps the pin.
//   q        everything back to idle (all ports 0xFF)
//
// Commands echo back, every phase is announced, so the serial log is the
// evidence. UART 9600 8N1 (Timer 1 mode 2), 12T @ 11.0592 MHz crystal.
// P3.0/P3.1 (UART/ISP) are never touched by probes.

#include <8051.h>

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

static void announce(const char *what, unsigned char port, unsigned char bit)
{
    putstr(what);
    putch(' ');
    putch('P');
    putch('0' + port);
    putch('.');
    putch('0' + bit);
    putstr("\r\n");
}

/* ~4.3 us per pass at 12T/11.0592 MHz (DJNZ-calibrated, 05-discover89) */
static void delay_us_ish(unsigned char passes)
{
    while (passes--) {
        __asm__("nop");
    }
}

static void delay_ms(unsigned int ms)
{
    while (ms--) {
        unsigned char i = 232;              /* ~1 ms of ~4.3 us passes */
        while (i--) {
            __asm__("nop");
        }
    }
}

static void pin_write(unsigned char port, unsigned char bit, unsigned char hi)
{
    unsigned char mask = (unsigned char)(1 << bit);
    switch (port) {
    case 0: if (hi) P0 |= mask; else P0 &= (unsigned char)~mask; break;
    case 1: if (hi) P1 |= mask; else P1 &= (unsigned char)~mask; break;
    case 2: if (hi) P2 |= mask; else P2 &= (unsigned char)~mask; break;
    case 3: if (hi) P3 |= mask; else P3 &= (unsigned char)~mask; break;
    }
}

static void idle_all(void)
{
    P0 = 0xFF;
    P1 = 0xFF;
    P2 = 0xFF;
    P3 |= 0xFC;                             /* leave UART bits alone */
}

static void buzz(unsigned char port, unsigned char bit)
{
    unsigned int cycles = 2000;             /* ~2 s at 1 kHz */

    if (port == 3 && bit < 2)
        return;                             /* never the UART pins */
    announce("BUZZ", port, bit);
    while (cycles--) {
        pin_write(port, bit, 0);
        delay_us_ish(116);                  /* ~500 us low */
        pin_write(port, bit, 1);
        delay_us_ish(116);                  /* ~500 us high */
    }
    idle_all();
    announce("BUZZ-DONE", port, bit);
}

static void digit_probe(unsigned char port, unsigned char bit)
{
    if (port == 3 && bit < 2)
        return;
    if (port == 0)
        return;                             /* P0 is the segment bus itself */
    announce("DIGIT", port, bit);
    P0 = 0x00;                              /* all segments to their ON level */
    pin_write(port, bit, 1);
    putstr("HI\r\n");
    delay_ms(1500);
    pin_write(port, bit, 0);
    putstr("LO\r\n");
    delay_ms(1500);
    idle_all();
    announce("DIGIT-DONE", port, bit);
}

static char getch(void)
{
    while (!RI)
        ;
    RI = 0;
    return SBUF;
}

void main(void)
{
    char cmd, cp, cb;

    SCON = 0x50;
    TMOD = (TMOD & 0x0F) | 0x20;
    TH1 = 0xFD;
    TL1 = 0xFD;
    TR1 = 1;

    idle_all();
    putstr("\r\nPROBE89 ready. b<p><b>=buzz d<p><b>=digit q=idle\r\n");

    for (;;) {
        cmd = getch();
        putch(cmd);                         /* echo = command received */
        if (cmd == 'q') {
            idle_all();
            putstr(" IDLE\r\n");
            continue;
        }
        if (cmd != 'b' && cmd != 'd')
            continue;
        cp = getch();
        putch(cp);
        cb = getch();
        putch(cb);
        putstr("\r\n");
        if (cp < '0' || cp > '3' || cb < '0' || cb > '7')
            continue;
        if (cmd == 'b')
            buzz((unsigned char)(cp - '0'), (unsigned char)(cb - '0'));
        else
            digit_probe((unsigned char)(cp - '0'), (unsigned char)(cb - '0'));
    }
}
