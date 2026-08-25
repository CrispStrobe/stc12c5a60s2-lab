// Prechin A2 DS1302 read-only retention test for STC89C52RC.
// This program deliberately contains no RTC write routine.

#include <8051.h>

#define LCD_DB P0
#define LCD_RW P2_5
#define LCD_RS P2_6
#define LCD_EN P2_7

#define RTC_IO P3_4
#define RTC_CE P3_5
#define RTC_CLK P3_6

#define TIMER0_1MS (65536u - 922u)

static unsigned char rtc[8];

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

static void lcd_write(unsigned char value, unsigned char is_data)
{
    LCD_EN = 0;
    LCD_RW = 0;
    LCD_RS = is_data;
    LCD_DB = value;
    delay_ms(1);
    LCD_EN = 1;
    delay_ms(2);
    LCD_EN = 0;
    delay_ms(2);
}

static void lcd_cmd(unsigned char value)
{
    lcd_write(value, 0);
    if (value == 0x01 || value == 0x02)
        delay_ms(5);
}

static void lcd_init(void)
{
    LCD_EN = 0;
    LCD_RS = 0;
    LCD_RW = 0;
    LCD_DB = 0xff;
    delay_ms(100);
    lcd_cmd(0x38);
    delay_ms(5);
    lcd_cmd(0x38);
    delay_ms(5);
    lcd_cmd(0x38);
    lcd_cmd(0x0c);
    lcd_cmd(0x01);
    lcd_cmd(0x06);
}

static void rtc_read_burst(void)
{
    unsigned char byte_number;
    unsigned char bit_number;
    unsigned char command = 0xbf; // clock-burst read, seconds through control

    RTC_CE = 0;
    RTC_CLK = 0;
    RTC_CE = 1;

    // DS1302 transfers both command and data least-significant bit first.
    for (bit_number = 0; bit_number < 8; ++bit_number) {
        RTC_IO = command & 1;
        command >>= 1;
        RTC_CLK = 1;
        __asm nop __endasm;
        RTC_CLK = 0;
        __asm nop __endasm;
    }

    RTC_IO = 1;                 // release quasi-bidirectional data line
    for (byte_number = 0; byte_number < 8; ++byte_number) {
        rtc[byte_number] = 0;
        for (bit_number = 0; bit_number < 8; ++bit_number) {
            rtc[byte_number] >>= 1;
            if (RTC_IO)
                rtc[byte_number] |= 0x80;
            RTC_CLK = 1;
            __asm nop __endasm;
            RTC_CLK = 0;
            __asm nop __endasm;
        }
    }

    RTC_CE = 0;
    RTC_CLK = 1;
    RTC_IO = 1;
}

static void lcd_hex(unsigned char value)
{
    static const char digits[] = "0123456789ABCDEF";
    lcd_write(digits[value >> 4], 1);
    lcd_write(digits[value & 0x0f], 1);
}

static void lcd_raw_field(char label, unsigned char value, char separator)
{
    lcd_write(label, 1);
    lcd_hex(value);
    lcd_write(separator, 1);
}

static void display_raw(void)
{
    // Raw hex makes invalid BCD and unstable reads visible instead of
    // converting 0xF nibbles into misleading '?' characters.
    lcd_cmd(0x80);
    lcd_raw_field('S', rtc[0], ' ');
    lcd_raw_field('M', rtc[1], ' ');
    lcd_raw_field('H', rtc[2], ' ');
    lcd_raw_field('D', rtc[3], ' ');
    lcd_cmd(0xc0);
    lcd_raw_field('N', rtc[4], ' '); // month
    lcd_raw_field('W', rtc[5], ' ');
    lcd_raw_field('Y', rtc[6], ' ');
    lcd_raw_field('C', rtc[7], ' '); // write-protect/control
}

void main(void)
{
    TMOD = (TMOD & 0xf0) | 0x01;
    RTC_CE = 0;
    RTC_CLK = 1;
    RTC_IO = 1;
    lcd_init();

    for (;;) {
        rtc_read_burst();
        display_raw();
        delay_ms(200);
    }
}
