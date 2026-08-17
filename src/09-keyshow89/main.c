// 09-keyshow89 — tonight's measurements, integrated: press any key on
// the A2's 4x4, its hex value (0-F) appears on the 7-seg AND echoes
// over the UART. Every constant in this file was measured on this
// board this evening, none assumed:
//
//   keypad  rows P1.7..P1.4 (top->bottom), cols P1.3..P1.0 (L->R)
//           [06-matrix89, six-point verified]
//   7-seg   COMMON CATHODE, segments on P0 via 74HC245,
//           digit select on P2.2..P2.4 via 74HC138  [07-sevenseg89]
//   LEDs    share P2 with the select  [08-ledfind89] — so the non-
//           select bits of P2 are held HIGH here; if the module is
//           active-low (unconfirmed), that keeps it dark.
//
// The matrix scan is the textbook one: drive one row low, read the
// columns. This is also the demo of the dialect gap noted in the
// ROADMAP — BrickWright pseudocode has no matrix-scan idiom yet, so
// this stays C until PART KEYPAD4x4 exists.

#include <8051.h>

#define TICK_50MS (65536u - 46080u)

/* common-cathode segment font, 0-F (a=bit0 .. g=bit6) */
static const unsigned char font[16] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07,
    0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71
};

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

/* one full scan; returns 0..15 or 0xFF for none */
static unsigned char scan_keypad(void)
{
    unsigned char row, cols;
    for (row = 0; row < 4; row++) {
        P1 = (unsigned char)~(0x80 >> row);   /* row 0 = P1.7 low */
        cols = P1 & 0x0F;                      /* cols P1.3..P1.0 */
        P1 = 0xFF;
        if (cols != 0x0F) {
            unsigned char col;
            for (col = 0; col < 4; col++)
                if (!(cols & (0x08 >> col)))   /* col 0 = P1.3 */
                    return row * 4 + col;      /* S1 -> 0 ... S16 -> 15 */
        }
    }
    return 0xFF;
}

void main(void)
{
    unsigned char key, shown = 0xFF;

    SCON = 0x50;
    TMOD = (TMOD & 0x0F) | 0x20;
    TH1 = 0xFD;
    TL1 = 0xFD;
    TR1 = 1;

    P2 = 0xE3;                  /* digit 0 selected; non-select bits high */
    P0 = 0x00;                  /* blank until first press */
    putstr("keyshow: press 4x4 keys\r\n");

    for (;;) {
        key = scan_keypad();
        if (key != 0xFF && key != shown) {
            delay_50ms_ticks(1);            /* settle */
            if (scan_keypad() == key) {     /* debounce: same key twice */
                shown = key;
                P0 = font[key];
                putstr("key S");
                if (key + 1 >= 10)
                    putch('0' + (key + 1) / 10);
                putch('0' + (key + 1) % 10);
                putstr(" -> ");
                putch(key < 10 ? '0' + key : 'A' + key - 10);
                putstr("\r\n");
            }
        }
        delay_50ms_ticks(1);
    }
}
