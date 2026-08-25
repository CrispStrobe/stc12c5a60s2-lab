// Prechin A2 ET/XPT2046 four-channel ADC display for STC89C52RC.
// AIN0=onboard 502 potentiometer; AIN1/AIN2=sensor networks; AIN3=external.

#include <8051.h>

#define LCD_DB P0
#define LCD_RW P2_5
#define LCD_RS P2_6
#define LCD_EN P2_7

#define ADC_DIN P3_4
#define ADC_CS P3_5
#define ADC_CLK P3_6
#define ADC_DOUT P3_7

#define TIMER0_1MS (65536u - 922u)

static const unsigned char adc_command[4] = { 0x94, 0xa4, 0xd4, 0xe4 };

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

static unsigned int adc_read(unsigned char command)
{
    unsigned char i;
    unsigned int value = 0;

    ADC_CS = 0;                 // also keeps DS1302 CE low/inactive
    ADC_CLK = 0;

    for (i = 0; i < 8; ++i) {
        ADC_DIN = (command & 0x80) != 0;
        command <<= 1;
        ADC_CLK = 1;
        __asm nop __endasm;
        ADC_CLK = 0;
        __asm nop __endasm;
    }

    ADC_DOUT = 1;               // release the quasi-bidirectional input
    for (i = 0; i < 12; ++i) {
        value <<= 1;
        ADC_CLK = 1;
        __asm nop __endasm;
        ADC_CLK = 0;
        __asm nop __endasm;
        if (ADC_DOUT)
            value |= 1;
    }

    ADC_CS = 1;
    return value;
}

static unsigned int adc_filtered(unsigned char command)
{
    unsigned char sample;
    unsigned int sum = 0;

    (void)adc_read(command);     // settle after channel/multiplexer change
    for (sample = 0; sample < 16; ++sample)
        sum += adc_read(command);
    return (sum + 8u) >> 4;
}

static void lcd_decimal4(unsigned int value)
{
    lcd_write('0' + (value / 1000u) % 10u, 1);
    lcd_write('0' + (value / 100u) % 10u, 1);
    lcd_write('0' + (value / 10u) % 10u, 1);
    lcd_write('0' + value % 10u, 1);
}

static void lcd_channel(char label, unsigned int value, char tail)
{
    lcd_write(label, 1);
    lcd_write(':', 1);
    lcd_decimal4(value);
    lcd_write(tail, 1);
}

void main(void)
{
    unsigned int value[4];
    unsigned char i;

    TMOD = (TMOD & 0xf0) | 0x01;
    ADC_CS = 1;
    ADC_CLK = 0;
    ADC_DIN = 1;
    ADC_DOUT = 1;
    lcd_init();

    for (;;) {
        for (i = 0; i < 4; ++i) {
            value[i] = adc_filtered(adc_command[i]);
        }

        lcd_cmd(0x80);
        lcd_channel('P', value[0], ' '); // potentiometer
        lcd_channel('N', value[1], ' '); // NTC temperature divider
        lcd_cmd(0xc0);
        lcd_channel('L', value[2], ' '); // GR/photoresistor divider
        lcd_channel('X', value[3], ' '); // external AIN3
        delay_ms(150);
    }
}
