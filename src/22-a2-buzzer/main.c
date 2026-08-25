// Prechin A2 BZ1 pin-isolation test for STC89C52RC.
//
// Physical-board silkscreen: BZ1=P2.5
// Recovered A2 schematic:     BZ1=P1.5
//
// Repeating audible sequence:
//   P1.5 -> one long 500 Hz burst
//   P2.5 -> two short 500 Hz bursts
// LCD EN is held low while P2.5 (also LCD RW) is exercised.

#include <8051.h>

#define LCD_DB P0
#define LCD_RW P2_5
#define LCD_RS P2_6
#define LCD_EN P2_7

#define BZ_SCHEMATIC P1_5
#define BZ_SILKSCREEN P2_5

#define TIMER0_1MS (65536u - 922u)

static void delay_ms(unsigned int ms)
{
    while (ms--) {
        TH0 = TIMER0_1MS >> 8;
        TL0 = TIMER0_1MS & 0xff;
        TF0 = 0;
        TR0 = 1;
        while (!TF0)
            ;
        TR0 = 0;
    }
}

static void lcd_pulse(void)
{
    LCD_EN = 1;
    delay_ms(2);
    LCD_EN = 0;
    delay_ms(2);
}

static void lcd_write(unsigned char value, unsigned char is_data)
{
    LCD_EN = 0;
    LCD_RW = 0;
    LCD_RS = is_data;
    LCD_DB = value;
    delay_ms(1);
    lcd_pulse();
}

static void lcd_cmd(unsigned char value)
{
    lcd_write(value, 0);
    if (value == 0x01 || value == 0x02)
        delay_ms(5);
}

static void lcd_text(unsigned char address, const char *text)
{
    lcd_cmd(0x80 | address);
    while (*text)
        lcd_write(*text++, 1);
}

static void lcd_init(void)
{
    LCD_EN = 0;
    LCD_RS = 0;
    LCD_RW = 0;
    LCD_DB = 0xff;
    delay_ms(100);

    // Same conservative 8-bit initialization convention as vendor example 18.
    lcd_cmd(0x38);
    delay_ms(5);
    lcd_cmd(0x38);
    delay_ms(5);
    lcd_cmd(0x38);
    lcd_cmd(0x0c);
    lcd_cmd(0x01);
    lcd_cmd(0x06);
}

static void burst_p15(unsigned int duration_ms)
{
    while (duration_ms--) {
        BZ_SCHEMATIC = !BZ_SCHEMATIC;
        delay_ms(1);
    }
    BZ_SCHEMATIC = 1;
}

static void burst_p25(unsigned int duration_ms)
{
    LCD_EN = 0;                 // P2.5 may change only while LCD is disabled
    while (duration_ms--) {
        BZ_SILKSCREEN = !BZ_SILKSCREEN;
        delay_ms(1);
    }
    BZ_SILKSCREEN = 0;          // normal write-only LCD idle state
}

void main(void)
{
    TMOD = (TMOD & 0xf0) | 0x01;
    BZ_SCHEMATIC = 1;
    LCD_EN = 0;
    LCD_RW = 0;

    lcd_init();
    lcd_text(0x00, "BZ1 PIN TEST    ");

    for (;;) {
        lcd_text(0x40, "P15: ONE LONG   ");
        delay_ms(700);
        burst_p15(700);
        delay_ms(1200);

        lcd_text(0x40, "P25: TWO SHORT  ");
        delay_ms(700);
        burst_p25(220);
        delay_ms(220);
        burst_p25(220);
        delay_ms(2500);
    }
}
