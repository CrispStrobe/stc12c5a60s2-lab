// Prechin A2 AT24C02 + independent K1..K4 diagnostic for STC89C52RC.
// EEPROM bytes F8..FB are saved, tested, and restored before PASS is shown.
// After flashing, remove both P5 UART shunts to isolate K1/P3.1 and K2/P3.0.

#include <8051.h>

#define LCD_DB P0
#define LCD_RW P2_5
#define LCD_RS P2_6
#define LCD_EN P2_7

#define I2C_SDA P2_0
#define I2C_SCL P2_1

#define KEY1 P3_1
#define KEY2 P3_0
#define KEY3 P3_2
#define KEY4 P3_3

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

static void i2c_delay(void)
{
    __asm
        nop
        nop
        nop
        nop
        nop
    __endasm;
}

static void i2c_start(void)
{
    I2C_SDA = 1;
    I2C_SCL = 1;
    i2c_delay();
    I2C_SDA = 0;
    i2c_delay();
    I2C_SCL = 0;
}

static void i2c_stop(void)
{
    I2C_SDA = 0;
    I2C_SCL = 1;
    i2c_delay();
    I2C_SDA = 1;
    i2c_delay();
}

static unsigned char i2c_send(unsigned char value)
{
    unsigned char i;
    unsigned char acknowledged;

    for (i = 0; i < 8; ++i) {
        I2C_SDA = (value & 0x80) != 0;
        value <<= 1;
        i2c_delay();
        I2C_SCL = 1;
        i2c_delay();
        I2C_SCL = 0;
    }

    I2C_SDA = 1;               // release SDA for slave ACK
    i2c_delay();
    I2C_SCL = 1;
    i2c_delay();
    acknowledged = !I2C_SDA;
    I2C_SCL = 0;
    return acknowledged;
}

static unsigned char i2c_read_nack(void)
{
    unsigned char i;
    unsigned char value = 0;

    I2C_SDA = 1;
    for (i = 0; i < 8; ++i) {
        value <<= 1;
        I2C_SCL = 1;
        i2c_delay();
        if (I2C_SDA)
            value |= 1;
        I2C_SCL = 0;
        i2c_delay();
    }

    I2C_SDA = 1;               // NACK the final byte
    I2C_SCL = 1;
    i2c_delay();
    I2C_SCL = 0;
    return value;
}

static unsigned char eeprom_wait_ready(void)
{
    unsigned char tries;
    for (tries = 0; tries < 100; ++tries) {
        unsigned char ready;
        i2c_start();
        ready = i2c_send(0xa0);
        i2c_stop();
        if (ready)
            return 1;
        delay_ms(1);
    }
    return 0;
}

static unsigned char eeprom_write(unsigned char address, unsigned char value)
{
    i2c_start();
    if (!i2c_send(0xa0) || !i2c_send(address) || !i2c_send(value)) {
        i2c_stop();
        return 0;
    }
    i2c_stop();
    return eeprom_wait_ready();
}

static unsigned char eeprom_read(unsigned char address, unsigned char *value)
{
    i2c_start();
    if (!i2c_send(0xa0) || !i2c_send(address)) {
        i2c_stop();
        return 0;
    }
    i2c_start();
    if (!i2c_send(0xa1)) {
        i2c_stop();
        return 0;
    }
    *value = i2c_read_nack();
    i2c_stop();
    return 1;
}

static unsigned char eeprom_test_and_restore(void)
{
    static const unsigned char pattern[4] = { 0xa5, 0x5a, 0x3c, 0xc3 };
    unsigned char saved[4];
    unsigned char value;
    unsigned char i;
    unsigned char ok = 1;

    for (i = 0; i < 4; ++i)
        if (!eeprom_read(0xf8 + i, &saved[i]))
            return 0;

    for (i = 0; i < 4; ++i) {
        if (!eeprom_write(0xf8 + i, pattern[i]) ||
            !eeprom_read(0xf8 + i, &value) || value != pattern[i])
            ok = 0;
    }

    // Always attempt restoration, even after a failed pattern verification.
    for (i = 0; i < 4; ++i) {
        if (!eeprom_write(0xf8 + i, saved[i]) ||
            !eeprom_read(0xf8 + i, &value) || value != saved[i])
            ok = 0;
    }
    return ok;
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
    lcd_cmd(0x38);
    delay_ms(5);
    lcd_cmd(0x38);
    delay_ms(5);
    lcd_cmd(0x38);
    lcd_cmd(0x0c);
    lcd_cmd(0x01);
    lcd_cmd(0x06);
}

static void display_keys(void)
{
    lcd_cmd(0xc0);
    lcd_text(0x40, "KEYS:");
    lcd_write(KEY1 ? '-' : '1', 1);
    lcd_write(KEY2 ? '-' : '2', 1);
    lcd_write(KEY3 ? '-' : '3', 1);
    lcd_write(KEY4 ? '-' : '4', 1);
    lcd_text(0x49, " P5OFF ");
}

void main(void)
{
    unsigned char eeprom_ok;

    TMOD = (TMOD & 0xf0) | 0x01;
    KEY1 = 1;
    KEY2 = 1;
    KEY3 = 1;
    KEY4 = 1;
    I2C_SDA = 1;
    I2C_SCL = 1;

    eeprom_ok = eeprom_test_and_restore();
    lcd_init();
    if (eeprom_ok)
        lcd_text(0x00, "EEPROM: PASS    ");
    else
        lcd_text(0x00, "EEPROM: FAIL    ");

    for (;;) {
        display_keys();
        delay_ms(30);
    }
}
