/*
 * stc15-extra.h — the STC15F2K60S2 registers SDCC does not ship.
 *
 * SDCC provides mcs51/stc12.h and mcs51/stc89.h and nothing else for STC parts,
 * so an STC15 build borrows stc12.h — which is sound, because 74 SFRs share
 * both name and address between the two families and none keeps its name at a
 * different address (docs/STC15-PERIPHERAL-MODEL.md §1). This header adds what
 * is genuinely new.
 *
 * ONLY the registers this part actually has. The datasheet's SFR address map is
 * family-wide, so it also lists Timers 3 and 4, UARTs 3 and 4 and the enhanced
 * PWM block — none of which exist on the STC15F2K60S2 (model §3). Declaring
 * them here because the map has them is exactly the mistake that makes a model
 * answer as a chip it is not.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef STC15_EXTRA_H
#define STC15_EXTRA_H

#ifndef PART_STC15F2K60S2
#error "stc15-extra.h is for PART=stc15f2k60s2 only"
#endif

#include <stc12.h>

/* ------------------------------------------------------------------ Timer 2
 * The STC12's dedicated baud-rate timer is gone; Timer 2 does that job, and
 * the AUXR bits that used to configure the BRT now configure it:
 *
 *   AUXR.4 BRTR   -> T2R      run
 *   AUXR.3 S2SMOD -> T2_C/T   counter vs timer
 *   AUXR.2 BRTx12 -> T2x12    1T vs 12T
 *   AUXR.0 S1BRS  -> S1ST2    UART1 takes its baud from Timer 2
 *
 * The roles line up, so the same AUXR bit pattern still means "run the baud
 * timer at 1T and use it for UART1". The reload does NOT line up: BRT was one
 * 8-bit register, this is two.
 */
SFR(T2H, 0xD6);
SFR(T2L, 0xD7);

/* --------------------------------------------------- peripheral pin switches
 * 0xA2 is AUXR1 on both parts, but on the STC15 it is the pin-remap register
 * and no longer carries ADRJ (which moved to CLK_DIV bit 5 — model §2.1).
 * stc12.h already declares AUXR1 at 0xA2; P_SW1 is the STC15 name for it.
 */
SFR(P_SW2, 0xBA);
SFR(BUS_SPEED, 0xA1);

/* ---------------------------------------------------- clock out / interrupts
 * 0x8F is WAKE_CLKO on the STC12 and INT_CLKO here, with different bits:
 * EX4 EX3 EX2 - T2CLKO T1CLKO T0CLKO. stc12.h declares the address already
 * under the other name, so this is an alias, not a second register.
 */
#define INT_CLKO   WAKE_CLKO

/* ------------------------------------------------------------ wake-up timer */
SFR(WKTCL, 0xAA);
SFR(WKTCH, 0xAB);

/* --------------------------------------------------------------------- SPI */
SFR(SPSTAT, 0xCD);
SFR(SPCTL,  0xCE);
SFR(SPDAT,  0xCF);

/* --------------------------------------------------------------- IAP/EEPROM
 * Present on the STC12 too, but stc12.h does not declare them.
 */
SFR(WDT_CONTR, 0xC1);
SFR(IAP_DATA,  0xC2);
SFR(IAP_ADDRH, 0xC3);
SFR(IAP_ADDRL, 0xC4);
SFR(IAP_CMD,   0xC5);
SFR(IAP_TRIG,  0xC6);
SFR(IAP_CONTR, 0xC7);

/* ------------------------------------------------- third CCP/PCA module
 * This part has 3 CCP/PCA channels; the STC12 has 2. stc12.h declares
 * modules 0 and 1.
 */
SFR(CCAPM2,   0xDC);
SFR(CCAP2L,   0xEC);
SFR(CCAP2H,   0xFC);
SFR(PCA_PWM2, 0xF4);

#endif /* STC15_EXTRA_H */
