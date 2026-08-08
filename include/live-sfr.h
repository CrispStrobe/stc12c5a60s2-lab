/*
 * live-sfr.h — the curated SFR window, and why it has to be curated.
 *
 * The 8051 has no indirect SFR addressing. `MOV A,direct` takes a *literal*
 * operand, so nothing on this chip can read "SFR number n" where n is a
 * variable. Reading an arbitrary SFR therefore costs either a ~1 KB table of
 * `MOV A,<sfr>` / `RET` stubs in flash, or a switch over the ones that
 * matter. docs/DEBUG-CONTROL-MODEL.md §6 chose the switch, and the set below
 * is exactly the SFR map from docs/STC12-PERIPHERAL-MODEL.md §2.
 *
 * This is the concrete shape of the asymmetry the capability matrix warns
 * about: an emulator answers `sfrs: 'all'`, and the chip cannot.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef LIVE_SFR_H
#define LIVE_SFR_H

#include <stc12.h>
#include "live-proto.h"

/*
 * Read one SFR. Returns 1 and stores the value if the address is in the
 * curated set, 0 if it is not — never a fabricated zero.
 */
static unsigned char live_sfr_read(unsigned char addr, unsigned char *out)
{
    switch (addr) {
    /* ---- core 8051 ---- */
    case 0x80: *out = P0;         return 1;
    case 0x81: *out = SP;         return 1;
    case 0x82: *out = DPL;        return 1;
    case 0x83: *out = DPH;        return 1;
    case 0x87: *out = PCON;       return 1;
    case 0x88: *out = TCON;       return 1;
    case 0x89: *out = TMOD;       return 1;
    case 0x8A: *out = TL0;        return 1;
    case 0x8B: *out = TL1;        return 1;
    case 0x8C: *out = TH0;        return 1;
    case 0x8D: *out = TH1;        return 1;
    case 0x90: *out = P1;         return 1;
    case 0x98: *out = SCON;       return 1;
    case 0x99: *out = SBUF;       return 1;
    case 0xA0: *out = P2;         return 1;
    case 0xA8: *out = IE;         return 1;
    case 0xB0: *out = P3;         return 1;
    case 0xB7: *out = IPH;        return 1;
    case 0xB8: *out = IP;         return 1;
    case 0xD0: *out = PSW;        return 1;
    case 0xE0: *out = ACC;        return 1;
    case 0xF0: *out = B;          return 1;

    /* ---- STC additions: clocking, ports, modes ---- */
    case 0x8E: *out = AUXR;       return 1;
    case 0x91: *out = P1M1;       return 1;
    case 0x92: *out = P1M0;       return 1;
    case 0x93: *out = P0M1;       return 1;
    case 0x94: *out = P0M0;       return 1;
    case 0x95: *out = P2M1;       return 1;
    case 0x96: *out = P2M0;       return 1;
    case 0x97: *out = CLK_DIV;    return 1;
    case 0xA2: *out = AUXR1;      return 1;
    case 0xB1: *out = P3M1;       return 1;
    case 0xB2: *out = P3M0;       return 1;
    case 0xB3: *out = P4M1;       return 1;
    case 0xB4: *out = P4M0;       return 1;
#ifndef PART_STC15F2K60S2
    case 0xBB: *out = P4SW;       return 1;   /* STC12 only — P_SW1/2 replace it */
#endif
    case 0xC0: *out = P4;         return 1;
    case 0xC8: *out = P5;         return 1;
    case 0xC9: *out = P5M1;       return 1;
    case 0xCA: *out = P5M0;       return 1;

    /* ---- serial 2, baud-rate timer, interrupt priority ---- */
    case 0x9A: *out = S2CON;      return 1;
    case 0x9B: *out = S2BUF;      return 1;
    case 0x9C: *out = BRT;        return 1;
    case 0xA9: *out = SADDR;      return 1;
    case 0xB6: *out = IP2H;       return 1;
    case 0xB9: *out = SADEN;      return 1;

    /* ---- ADC (peripheral model §4 — unverified on silicon) ---- */
    case 0x9D: *out = P1ASF;      return 1;
    case 0xBC: *out = ADC_CONTR;  return 1;
    case 0xBD: *out = ADC_RES;    return 1;
    case 0xBE: *out = ADC_RESL;   return 1;

    /* ---- PCA / PWM (peripheral model §5 — skeleton) ---- */
    case 0xD8: *out = CCON;       return 1;
    case 0xD9: *out = CMOD;       return 1;
    case 0xDA: *out = CCAPM0;     return 1;
    case 0xDB: *out = CCAPM1;     return 1;
    case 0xE9: *out = CL;         return 1;
    case 0xF2: *out = PCA_PWM0;   return 1;
    case 0xF3: *out = PCA_PWM1;   return 1;
    case 0xF9: *out = CH;         return 1;
    case 0xFA: *out = CCAP0H;     return 1;
    case 0xFB: *out = CCAP1H;     return 1;

#ifdef PART_STC15F2K60S2
    /* ---- STC15F2K60S2 additions (peripheral model §3) ----
     * Only what this part actually has. Timers 3/4, UARTs 3/4 and the enhanced
     * PWM block are in the family address map but not on this die, so they stay
     * out: a window that answers for a register the chip lacks is worse than
     * one that says "not in the set". */
    case 0xA1: *out = BUS_SPEED;  return 1;
    case 0xAA: *out = WKTCL;      return 1;
    case 0xAB: *out = WKTCH;      return 1;
    case 0xBA: *out = P_SW2;      return 1;
    case 0xC1: *out = WDT_CONTR;  return 1;
    case 0xC2: *out = IAP_DATA;   return 1;
    case 0xC3: *out = IAP_ADDRH;  return 1;
    case 0xC4: *out = IAP_ADDRL;  return 1;
    case 0xC5: *out = IAP_CMD;    return 1;
    case 0xC7: *out = IAP_CONTR;  return 1;
    case 0xCD: *out = SPSTAT;     return 1;
    case 0xCE: *out = SPCTL;      return 1;
    case 0xCF: *out = SPDAT;      return 1;
    case 0xD6: *out = T2H;        return 1;
    case 0xD7: *out = T2L;        return 1;
    case 0xDC: *out = CCAPM2;     return 1;
    case 0xEC: *out = CCAP2L;     return 1;
    case 0xF4: *out = PCA_PWM2;   return 1;
    case 0xFC: *out = CCAP2H;     return 1;
    /* 0xC6 IAP_TRIG is deliberately absent: it is write-only and reading it is
     * meaningless, while a stray WRITE to it commits a flash operation. */
#endif

    default:
        return 0;
    }
}

/*
 * Write one SFR. Returns 0 on success, or a LIVE_ERR_* code.
 *
 * The refusals are not paternalism: every one of these registers is part of
 * the link the monitor is answering over, or part of the clock that makes
 * halting reversible. Writing them would end the debug session mid-frame,
 * with no way to report what happened. §6 of the model requires that a
 * target refuse with a reason rather than comply and disappear.
 */
static unsigned char live_sfr_write(unsigned char addr, unsigned char val)
{
    switch (addr) {
    case 0x87:   /* PCON  — SMOD, and power-down would end the session   */
    case 0x89:   /* TMOD  — both timers, including the one being frozen  */
    case 0x8E:   /* AUXR  — S1BRS/BRTR/BRTx12 are the UART clock         */
    case 0x98:   /* SCON  — the UART itself                              */
    case 0x99:   /* SBUF  — would inject a byte into a reply             */
    case 0x9C:   /* BRT   — the baud rate                                */
    case 0xA8:   /* IE    — clearing EA stops the tick that resumes us   */
#ifdef PART_STC15F2K60S2
    case 0xD6:   /* T2H   — the STC15's baud rate lives here instead     */
    case 0xD7:   /* T2L                                                  */
    case 0xC5:   /* IAP_CMD  — arming a flash erase from a debug write   */
    case 0xC6:   /* IAP_TRIG — and the register that would commit it     */
    case 0xC7:   /* IAP_CONTR                                            */
#endif
        return LIVE_ERR_REFUSED;

    case 0x80: P0        = val; return 0;
    case 0x81: SP        = val; return 0;
    case 0x82: DPL       = val; return 0;
    case 0x83: DPH       = val; return 0;
    case 0x88: TCON      = val; return 0;
    case 0x8A: TL0       = val; return 0;
    case 0x8B: TL1       = val; return 0;
    case 0x8C: TH0       = val; return 0;
    case 0x8D: TH1       = val; return 0;
    case 0x90: P1        = val; return 0;
    case 0xA0: P2        = val; return 0;
    case 0xB0: P3        = val; return 0;
    case 0xB7: IPH       = val; return 0;
    case 0xB8: IP        = val; return 0;
    case 0xD0: PSW       = val; return 0;
    case 0xE0: ACC       = val; return 0;
    case 0xF0: B         = val; return 0;

    case 0x91: P1M1      = val; return 0;
    case 0x92: P1M0      = val; return 0;
    case 0x93: P0M1      = val; return 0;
    case 0x94: P0M0      = val; return 0;
    case 0x95: P2M1      = val; return 0;
    case 0x96: P2M0      = val; return 0;
    case 0x97: CLK_DIV   = val; return 0;
    case 0xA2: AUXR1     = val; return 0;
    case 0xB1: P3M1      = val; return 0;
    case 0xB2: P3M0      = val; return 0;
    case 0xB3: P4M1      = val; return 0;
    case 0xB4: P4M0      = val; return 0;
    case 0xBB: P4SW      = val; return 0;
    case 0xC0: P4        = val; return 0;
    case 0xC8: P5        = val; return 0;
    case 0xC9: P5M1      = val; return 0;
    case 0xCA: P5M0      = val; return 0;

    case 0x9A: S2CON     = val; return 0;
    case 0x9B: S2BUF     = val; return 0;
    case 0xA9: SADDR     = val; return 0;
    case 0xB6: IP2H      = val; return 0;
    case 0xB9: SADEN     = val; return 0;

    case 0x9D: P1ASF     = val; return 0;
    case 0xBC: ADC_CONTR = val; return 0;
    case 0xBD: ADC_RES   = val; return 0;
    case 0xBE: ADC_RESL  = val; return 0;

    case 0xD8: CCON      = val; return 0;
    case 0xD9: CMOD      = val; return 0;
    case 0xDA: CCAPM0    = val; return 0;
    case 0xDB: CCAPM1    = val; return 0;
    case 0xE9: CL        = val; return 0;
    case 0xF2: PCA_PWM0  = val; return 0;
    case 0xF3: PCA_PWM1  = val; return 0;
    case 0xF9: CH        = val; return 0;
    case 0xFA: CCAP0H    = val; return 0;
    case 0xFB: CCAP1H    = val; return 0;

    default:
        return LIVE_ERR_SPACE;
    }
}

#endif /* LIVE_SFR_H */
