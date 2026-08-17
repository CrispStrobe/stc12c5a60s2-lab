// 12-lcd89 v2 — the A2's LCD1602, single-convention, conservative.
// v1 swept six RS/RW/EN permutations and got row-of-blocks: a wrong
// permutation can leave the controller output-driving the bus, and
// the sweep poisons its own next attempt. The pin truth is now
// TRIPLE-sourced (A2 silkscreen "RW/RS/EN P25-27", hongwenjun's
// lcd1602a.h, markchan3, byr balance): DB=P0, RW=P2.5, RS=P2.6,
// EN=P2.7, execute on EN's falling edge. So v2 does exactly that,
// with datasheet-conservative timing (100 ms power-on, 5 ms between
// the three 0x38s) and BUSY-FLAG polling before every write like the
// reference driver. The banner rewrites every second — whatever
// state the glass is in, it recovers within one.
//
//   line 1:  A2 LCD1602 OK
//   line 2:  perm RW RS EN 567

#include <8051.h>

#define LCD_DB P0
#define LCD_RW P2_5
#define LCD_RS P2_6
#define LCD_EN P2_7

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

static void dwell(unsigned char loops)
{
    while (loops--)
        __asm__("nop");
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

static void puthex(unsigned char v)
{
    static const char hex[] = "0123456789ABCDEF";
    putch(hex[v >> 4]);
    putch(hex[v & 0x0F]);
}

static void lcd_busy(void)
{
    unsigned int guard = 0;
    LCD_DB = 0xFF;              /* release the quasi bus for reading */
    LCD_RS = 0;
    LCD_RW = 1;
    LCD_EN = 1;
    while (LCD_DB & 0x80) {
        if (++guard >= 1000)
            break;              /* no LCD answering: don't hang */
    }
    LCD_EN = 0;
    LCD_RW = 0;
}

static void lcd_cmd(unsigned char cmd)
{
    lcd_busy();
    LCD_RS = 0;
    LCD_RW = 0;
    LCD_DB = cmd;
    LCD_EN = 1;
    dwell(10);
    LCD_EN = 0;
}

static void lcd_dat(unsigned char dat)
{
    lcd_busy();
    LCD_RS = 1;
    LCD_RW = 0;
    LCD_DB = dat;
    LCD_EN = 1;
    dwell(10);
    LCD_EN = 0;
}

static void lcd_text(unsigned char addr, const char *s)
{
    lcd_cmd(0x80 | addr);
    while (*s)
        lcd_dat(*s++);
}

void main(void)
{
    SCON = 0x50;
    TMOD = (TMOD & 0x0F) | 0x20;
    TH1 = 0xFD;
    TL1 = 0xFD;
    TR1 = 1;

    delay_50ms_ticks(2);        /* >100 ms after power for the HD44780 */
    lcd_cmd(0x38);              /* 8-bit, 2 lines, 5x8 */
    delay_50ms_ticks(1);        /* datasheet wants >4.1 ms here */
    lcd_cmd(0x38);
    delay_50ms_ticks(1);
    lcd_cmd(0x38);
    lcd_cmd(0x0C);              /* display on, no cursor */
    lcd_cmd(0x01);              /* clear */
    delay_50ms_ticks(1);
    lcd_cmd(0x06);              /* entry mode: increment */

    putstr("lcd v3 up\r\n");

    for (;;) {
        unsigned char st1, st2, d1, d2;
        lcd_text(0x00, "A2 LCD1602 OK");
        lcd_text(0x40, "perm RW RS EN567");

        /* interrogate: status (busy+AC), then DDRAM back from 0x00 */
        LCD_DB = 0xFF; LCD_RS = 0; LCD_RW = 1; LCD_EN = 1; dwell(10);
        st1 = LCD_DB; LCD_EN = 0; dwell(10);
        lcd_cmd(0x80);              /* AC := 0 */
        LCD_DB = 0xFF; LCD_RS = 0; LCD_RW = 1; LCD_EN = 1; dwell(10);
        st2 = LCD_DB; LCD_EN = 0; dwell(10);
        LCD_DB = 0xFF; LCD_RS = 1; LCD_RW = 1; LCD_EN = 1; dwell(10);
        d1 = LCD_DB; LCD_EN = 0; dwell(10);
        LCD_DB = 0xFF; LCD_RS = 1; LCD_RW = 1; LCD_EN = 1; dwell(10);
        d2 = LCD_DB; LCD_EN = 0; dwell(10);
        LCD_RW = 0;

        putstr("st=");
        puthex(st1); putch(' '); puthex(st2);
        putstr(" ddram=");
        puthex(d1); putch(' '); puthex(d2);
        putstr("\r\n");
        delay_50ms_ticks(20);
    }
}
