// 12-lcd89 v5 — A2 LCD1602 test derived from the HC6800EM3 vendor DVD.
//
// The A2 schematic and DVD MCS51 examples agree:
//   DB0..DB7=P0, RW=P2.5, RS=P2.6, EN=P2.7.
// This version uses the DVD example's 4-bit bus and deliberately enormous
// setup/enable times. It never reads from the LCD. Bench isolation later
// confirmed that this board revision's buzzer shares LCD_RW/P2.5; the
// recovered schematic's BZ1=P1.5 assignment does not apply here.

#include <8051.h>

#define LCD_DB P0
#define LCD_RW P2_5
#define LCD_RS P2_6
#define LCD_EN P2_7

#define TIMER0_1MS (65536u - 922u)

static void delay_ms(unsigned int ms)
{
    while (ms--) {
        TH0 = TIMER0_1MS >> 8;
        TL0 = TIMER0_1MS & 0xFF;
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

/* DB4..DB7 are P0.4..P0.7. Low P0 bits remain high/released. */
static void lcd_nibble(unsigned char high_nibble)
{
    LCD_EN = 0;
    LCD_DB = (high_nibble & 0xF0) | 0x0F;
    delay_ms(1);                /* vendor DVD: settle for a full millisecond */
    LCD_EN = 1;
    delay_ms(5);                /* vendor DVD: five-millisecond enable pulse */
    LCD_EN = 0;
    delay_ms(1);
}

static void lcd_write(unsigned char value, unsigned char is_data)
{
    LCD_RW = 0;
    LCD_RS = is_data;
    lcd_nibble(value);
    lcd_nibble(value << 4);
    delay_ms(2);
}

static void lcd_cmd(unsigned char cmd)
{
    lcd_write(cmd, 0);
    if (cmd == 0x01 || cmd == 0x02)
        delay_ms(5);
}

static void lcd_dat(unsigned char dat)
{
    lcd_write(dat, 1);
}

static void lcd_text(unsigned char addr, const char *s)
{
    lcd_cmd(0x80 | addr);
    while (*s)
        lcd_dat(*s++);
}

static void lcd_init(void)
{
    LCD_EN = 0;
    LCD_RS = 0;
    LCD_RW = 0;
    LCD_DB = 0xFF;
    delay_ms(100);

    /* HD44780 reset from an unknown state, then select a 4-bit interface. */
    lcd_nibble(0x30);
    delay_ms(5);
    lcd_nibble(0x30);
    delay_ms(5);
    lcd_nibble(0x30);
    delay_ms(2);
    lcd_nibble(0x20);

    lcd_cmd(0x28);              /* 4-bit, 2 lines, 5x8 */
    lcd_cmd(0x08);              /* display off while configuring */
    lcd_cmd(0x01);              /* clear */
    lcd_cmd(0x06);              /* increment, no shift */
    lcd_cmd(0x0C);              /* display on, cursor off */
}

void main(void)
{
    TMOD = 0x21;
    SCON = 0x50;
    TH1 = 0xFD;
    TL1 = 0xFD;
    TR1 = 1;

    lcd_init();
    lcd_text(0x00, "A2 LCD1602 OK   ");
    lcd_text(0x40, "DVD 4BIT P25-27");
    putstr("lcd v5: DVD-style 4-bit init complete\r\n");

    for (;;)
        ;
}
