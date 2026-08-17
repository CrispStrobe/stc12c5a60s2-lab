// 06-matrix89 — discovery v2: active matrix scan. v1 (05-discover89)
// watches pin LEVELS, which finds keys wired to ground (the minimum-
// system board's K1..K4) but is blind to a matrix keypad: pressing a
// matrix key connects a ROW PIN to a COLUMN PIN, and with every pin
// idling quasi-high, high-shorted-to-high moves nothing. Proven live
// on the Prechin A2 (2026-08-17): 4x4 presses, empty log.
//
// So v2 drives each candidate pin low in turn and reports which other
// pins FOLLOW it low. A pressed matrix key shows as a pair:
//   "P1.7~P1.3=1"  (press)   "P1.7~P1.3=0"  (release)
// A boot-time baseline pass absorbs structural couplings (buffers,
// modules) so only CHANGES against that baseline are reported. The v1
// static watcher stays, for ground-wired keys: "P3.2=0".
//
// P3.0/P3.1 (UART/ISP) are never driven and never reported. Driving a
// quasi pin low into a module input is what every real keypad scanner
// does; the momentary sink is within the port's rating.
//
// RAM: two 30x4 follower maps live in __xdata (the RC's 256-byte aux
// XRAM) — the 128-byte idata would not hold them.

#include <8051.h>

#define TICK_50MS (65536u - 46080u)

static void delay_short(unsigned char n)
{
    while (n--)
        __asm__("nop");
}

/* ---------- UART (Timer 1 mode 2, 9600) ---------- */

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

static void putpin(unsigned char port, unsigned char bit)
{
    putch('P');
    putch('0' + port);
    putch('.');
    putch('0' + bit);
}

/* ---------- ports ---------- */

static unsigned char lat[4] = { 0xFF, 0xFF, 0xFF, 0xFF };

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

/* candidate pins: all of P0..P3 except P3.0/P3.1 */
#define NPINS 30
static unsigned char pin_port(unsigned char i) { return i >> 3; }
static unsigned char pin_bit(unsigned char i)
{
    unsigned char b = i & 7;
    return (i >> 3) == 3 ? b + 2 : b;   /* P3 slots start at P3.2 */
}
/* i = 24..29 -> P3.2..P3.7; i = 0..23 -> P0.0..P2.7 */

static __xdata unsigned char base[NPINS][4];
static __xdata unsigned char prev[NPINS][4];

/* drive pin i low, sample all ports into out[4], release */
static void sample_with_low(unsigned char i, unsigned char *out)
{
    unsigned char p = pin_port(i);
    unsigned char mask = 1 << pin_bit(i);
    unsigned char q;
    port_write(p, lat[p] & ~mask);
    delay_short(20);
    for (q = 0; q < 4; q++)
        out[q] = port_read(q);
    port_write(p, lat[p]);
    delay_short(40);                    /* quasi pins need recovery time */
}

void main(void)
{
    unsigned char smp[4], last[4];
    unsigned char i, q, b, diff, now;

    SCON = 0x50;
    TMOD = (TMOD & 0x0F) | 0x20;
    TH1 = 0xFD;
    TL1 = 0xFD;
    TR1 = 1;

    P0 = P1 = P2 = P3 = 0xFF;

    putstr("\r\nmatrix89: baseline...\r\n");
    for (i = 0; i < NPINS; i++) {
        sample_with_low(i, smp);
        for (q = 0; q < 4; q++) {
            base[i][q] = smp[q];
            prev[i][q] = smp[q];
        }
    }
    putstr("scanning - press keys (matrix or direct)\r\n");

    for (q = 0; q < 4; q++)
        last[q] = port_read(q);
    last[3] |= 0x03;

    for (;;) {
        /* v1 static watcher: ground-wired keys */
        for (q = 0; q < 4; q++) {
            now = port_read(q);
            if (q == 3)
                now |= 0x03;
            diff = now ^ last[q];
            if (diff) {
                delay_short(200);
                if (((port_read(q) ^ last[q]) & diff) == diff) {
                    for (b = 0; b < 8; b++) {
                        if (diff & (1 << b)) {
                            putpin(q, b);
                            putch('=');
                            putch('0' + ((now >> b) & 1));
                            putstr("\r\n");
                        }
                    }
                    last[q] = now;
                }
            }
        }

        /* v2 matrix scan: report follower changes vs baseline */
        for (i = 0; i < NPINS; i++) {
            sample_with_low(i, smp);
            for (q = 0; q < 4; q++) {
                if (q == 3)
                    smp[q] |= 0x03;
                /* a follower is a pin LOW now that is HIGH in baseline */
                diff = (base[i][q] & ~smp[q]) ^ (base[i][q] & ~prev[i][q]);
                if (!diff) {
                    prev[i][q] = smp[q];
                    continue;
                }
                for (b = 0; b < 8; b++) {
                    if (!(diff & (1 << b)))
                        continue;
                    if (q == pin_port(i) && b == pin_bit(i))
                        continue;
                    putpin(pin_port(i), pin_bit(i));
                    putch('~');
                    putpin(q, b);
                    putch('=');
                    putch((base[i][q] & ~smp[q] & (1 << b)) ? '1' : '0');
                    putstr("\r\n");
                }
                prev[i][q] = smp[q];
            }
        }
    }
}
