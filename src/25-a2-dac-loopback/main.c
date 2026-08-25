// Prechin A2 DAC1 -> J52 -> AIN3 closed-loop test for STC89C52RC.
// Fit the J52 shunt between the pins labelled DAC and IN3.

#include <8051.h>

#define LCD_DB P0
#define LCD_RW P2_5
#define LCD_RS P2_6
#define LCD_EN P2_7

#define PWM_DAC P2_1
#define ADC_DIN P3_4
#define ADC_CS P3_5
#define ADC_CLK P3_6
#define ADC_DOUT P3_7

#define TIMER0_1MS (65536u - 922u)
#define TIMER1_100US (256u - 92u)

static volatile unsigned char pwm_phase;
static volatile unsigned char pwm_duty;

void timer1_isr(void) __interrupt(3)
{
    if (++pwm_phase >= 100)
        pwm_phase = 0;
    PWM_DAC = pwm_phase < pwm_duty;
}

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

static unsigned int adc_read_ain3(void)
{
    unsigned char i;
    unsigned char command = 0xe4;
    unsigned int value = 0;

    ADC_CS = 0;
    ADC_CLK = 0;
    for (i = 0; i < 8; ++i) {
        ADC_DIN = (command & 0x80) != 0;
        command <<= 1;
        ADC_CLK = 1;
        __asm nop __endasm;
        ADC_CLK = 0;
        __asm nop __endasm;
    }
    ADC_DOUT = 1;
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

static unsigned int adc_average(void)
{
    unsigned char i;
    unsigned int sum = 0;
    (void)adc_read_ain3();
    for (i = 0; i < 16; ++i)
        sum += adc_read_ain3();
    return (sum + 8u) >> 4;
}

static void lcd_uint4(unsigned int value)
{
    lcd_write('0' + (value / 1000u) % 10u, 1);
    lcd_write('0' + (value / 100u) % 10u, 1);
    lcd_write('0' + (value / 10u) % 10u, 1);
    lcd_write('0' + value % 10u, 1);
}

static void display(unsigned char duty, unsigned int adc)
{
    lcd_cmd(0x80);
    lcd_write('D', 1); lcd_write('A', 1); lcd_write('C', 1);
    lcd_write(' ', 1); lcd_write('P', 1); lcd_write('W', 1); lcd_write('M', 1);
    lcd_write(':', 1); lcd_write(' ', 1);
    lcd_write('0' + duty / 100, 1);
    lcd_write('0' + (duty / 10) % 10, 1);
    lcd_write('0' + duty % 10, 1);
    lcd_write('%', 1); lcd_write(' ', 1); lcd_write(' ', 1); lcd_write(' ', 1);

    lcd_cmd(0xc0);
    lcd_write('A', 1); lcd_write('I', 1); lcd_write('N', 1); lcd_write('3', 1);
    lcd_write(':', 1); lcd_write(' ', 1);
    lcd_uint4(adc);
    lcd_write(' ', 1); lcd_write('J', 1); lcd_write('5', 1); lcd_write('2', 1);
    lcd_write(' ', 1); lcd_write(' ', 1); lcd_write(' ', 1);
}

void main(void)
{
    static const unsigned char steps[5] = { 0, 25, 50, 75, 100 };
    unsigned char step;

    TMOD = 0x21;                // T1 mode 2 PWM tick, T0 mode 1 delay
    TH1 = TIMER1_100US;
    TL1 = TIMER1_100US;
    ET1 = 1;
    EA = 1;
    TR1 = 1;

    ADC_CS = 1;
    ADC_CLK = 0;
    ADC_DIN = 1;
    ADC_DOUT = 1;
    pwm_phase = 0;
    pwm_duty = 0;
    lcd_init();

    for (;;) {
        for (step = 0; step < 5; ++step) {
            pwm_duty = steps[step];
            delay_ms(700);      // allow the RC filter and LM358 to settle
            display(pwm_duty, adc_average());
            delay_ms(1300);
        }
    }
}
