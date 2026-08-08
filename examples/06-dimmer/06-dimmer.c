/* Generated from BrickWright pseudocode by stc-compiler.
 * Hand edits will be lost; change the pseudocode instead. */
#include <stc12.h>

#define FOSC_HZ 11059200UL

/* Timer 0, mode 1, clocked at FOSC/12 -- accuracy depends only on
 * FOSC, and every supported family counts this mode identically, so
 * the same program is timing-correct on a 12T STC89 and a 1T STC12
 * or STC15. Nothing in the generated code ever busy-waits. */
#define T0_RELOAD (65536UL - (FOSC_HZ / 12UL / 1000UL))

static void delay_ms(unsigned int ms)
{
    while (ms--) {
        TL0 = (unsigned char)(T0_RELOAD & 0xFF);
        TH0 = (unsigned char)(T0_RELOAD >> 8);
        TF0 = 0;
        TR0 = 1;
        while (!TF0) ;
        TR0 = 0;
        TF0 = 0;
    }
}

/* 10-bit ADC, polled. Channel n is on P1.n; the channel is selected
 * and the conversion started in one write, as STC's examples do. */
static unsigned int adc_read(unsigned char channel)
{
    unsigned char settle;
    ADC_CONTR = (unsigned char)(0xE8 | channel);  /* power|fast|start|chan */
    for (settle = 0; settle < 8; settle++) ;      /* let the mux settle */
    while (!(ADC_CONTR & 0x10)) ;                 /* wait for ADC_FLAG */
    ADC_CONTR &= ~0x10;                           /* clear it by hand */
    return ((unsigned int)ADC_RES << 2) | (ADC_RESL & 0x03);
}

/* PCA PWM. The comparator is 9 bits, {EPCnH,CCAPnH} against (0,CL),
 * and it drives the pin LOW while CL is BELOW the compare value -- so a
 * LARGER value is a LONGER low time and the duty as a fraction HIGH is
 * (256 - value)/256. Getting that backwards inverts every brightness and
 * looks entirely plausible doing it.
 *
 * Writing CCAPnH rather than CCAPnL is deliberate: the hardware reloads
 * CCAPnH into CCAPnL when CL wraps, so an update cannot glitch mid-period.
 * The 9th bit (EPCnH) is what expresses 0% and 100%, which an 8-bit
 * compare cannot. Datasheet 10.3.4. */
static void pwm_set(unsigned char module, unsigned int percent_high)
{
    unsigned int v;
    if (percent_high > 100) percent_high = 100;
    v = 256 - ((percent_high * 256 + 50) / 100);
    if (module == 0) {
        CCAP0H = (unsigned char)v;
        if (v > 255) PCA_PWM0 |= 0x02; else PCA_PWM0 &= (unsigned char)~0x02;
    } else {
        CCAP1H = (unsigned char)v;
        if (v > 255) PCA_PWM1 |= 0x02; else PCA_PWM1 &= (unsigned char)~0x02;
    }
}

void main(void)
{
    P1M1 &= ~0x08;   /* push-pull */
    P1M0 |=  0x08;

    P1ASF = 0x04;                 /* analog function on P1 */
    P1M1 |=  0x04;                /* high-impedance input */
    P1M0 &= ~0x04;
    ADC_CONTR = 0xE0;              /* ADC on, fastest conversion */

    CCON = 0x00;                   /* PCA off while configuring */
    CL = 0;  CH = 0;
    CMOD = 0x00;                   /* CPS=000: PCA clock = FOSC/12 */
    CCAPM0 = 0x42;                /* ECOM|PWM: lamp */
    pwm_set(0, 100 - (0));   /* lamp off */
    CCON = 0x40;                   /* CR: run the PCA counter */

    AUXR &= ~0x80;                 /* Timer 0 at FOSC/12 */
    TMOD  = (TMOD & 0xF0) | 0x01;  /* Timer 0, mode 1 */

    for (;;) {
        pwm_set(0, 100 - (adc_read(2) * 100 / 1023));
        delay_ms(20);
    }
}
