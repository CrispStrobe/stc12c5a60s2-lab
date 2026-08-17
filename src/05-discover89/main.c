// 05-discover89 — let the board document itself. The AliExpress "51
// minimum system" boards wire their 4 buttons, DS18B20, and IR
// receiver to pins that vary by revision and are documented nowhere
// trustworthy. So: probe, don't guess.
//
//  1. On boot, every candidate pin gets a 1-Wire reset; a DS18B20
//     answers with a presence pulse. Pins that answer are reported.
//     (P0 without external pull-ups reads low and would false-
//     positive; the report is a shortlist for a human, not an oracle.)
//  2. Then all four ports are watched; any pin change is reported as
//     "P3.2=0" — press a button, read its address off the wire.
//
// P3.0/P3.1 stay untouched (UART + ISP). Quasi-bidirectional pins are
// written 1 first so they read as inputs — the STC89 has no port-mode
// registers, this IS the input idiom on the classic core.
//
// Timing: 11.0592 MHz / 12T = 921 600 machine cycles/s, 1.085 us per
// cycle. The us delays are DJNZ-calibrated to that; the UART is
// Timer 1 mode 2 at 9600 like 04-hello89.

#include <8051.h>

/* ---------- UART (Timer 1 mode 2, 9600 @ 11.0592 MHz) ---------- */

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

static void putpin(unsigned char port, unsigned char bit, unsigned char val)
{
    putch('P');
    putch('0' + port);
    putch('.');
    putch('0' + bit);
    putch('=');
    putch('0' + val);
    putstr("\r\n");
}

/* ---------- microsecond-ish delay (DJNZ, 12T) ---------- */

static void delay_us(unsigned char us)
{
    // each loop pass is ~4 machine cycles = ~4.3 us; close enough for
    // 1-Wire's generous reset windows (min 480 us low)
    us >>= 2;
    while (us--) {
        __asm__("nop");
    }
}

static void delay_480us(void)
{
    unsigned char i;
    for (i = 0; i < 8; i++)
        delay_us(240);              // 8 * ~60 us... calibrated below
}

/* ---------- port access by index (8051 SFRs are not addressable
 * indirectly, so a switch it is) ---------- */

static unsigned char port_read(unsigned char p)
{
    switch (p) {
    case 0: return P0;
    case 1: return P1;
    case 2: return P2;
    default: return P3;
    }
}

static void port_write(unsigned char p, unsigned char v)
{
    switch (p) {
    case 0: P0 = v; break;
    case 1: P1 = v; break;
    case 2: P2 = v; break;
    default: P3 = v; break;
    }
}

/* ---------- 1-Wire presence probe on one pin ---------- */

static unsigned char onewire_presence(unsigned char p, unsigned char bit)
{
    unsigned char mask = 1 << bit;
    unsigned char present;

    port_write(p, port_read(p) & ~mask);    // drive low
    delay_480us();                          // reset pulse >= 480 us
    port_write(p, port_read(p) | mask);     // release
    delay_us(70);                           // presence window
    present = !(port_read(p) & mask);       // device pulls low = present
    delay_480us();                          // let the bus recover
    return present;
}

void main(void)
{
    unsigned char last[4], now, prev, p, b, changed;

    SCON = 0x50;
    TMOD = (TMOD & 0x0F) | 0x20;
    TH1 = 0xFD;
    TL1 = 0xFD;
    TR1 = 1;

    P0 = P1 = P2 = 0xFF;                    // all quasi pins to input mode
    P3 |= 0xFC;                             // P3.2..P3.7 too; UART bits alone

    putstr("\r\ndiscover89: 1-wire probe...\r\n");
    for (p = 0; p < 4; p++) {
        for (b = 0; b < 8; b++) {
            if (p == 3 && b < 2)
                continue;                   // UART/ISP pins
            if (onewire_presence(p, b)) {
                putstr("presence ");
                putpin(p, b, 1);
            }
        }
    }
    putstr("watching pins - press buttons now\r\n");

    for (p = 0; p < 4; p++)
        last[p] = port_read(p);
    last[3] |= 0x03;                        // never report the UART pins

    for (;;) {
        for (p = 0; p < 4; p++) {
            now = port_read(p);
            if (p == 3)
                now |= 0x03;
            prev = last[p];
            changed = now ^ prev;
            if (!changed)
                continue;
            // settle: only believe a change that survives ~half a ms —
            // bounce may still print twice, which discovery can live with
            delay_us(255);
            delay_us(255);
            if (((port_read(p) ^ prev) & changed) != changed)
                continue;
            for (b = 0; b < 8; b++) {
                if (changed & (1 << b))
                    putpin(p, b, (now >> b) & 1);
            }
            last[p] = now;
        }
    }
}
