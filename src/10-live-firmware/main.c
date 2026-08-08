/*
 * 10-live-firmware — the on-chip half of boundary D.
 *
 * docs/DEBUG-CONTROL-MODEL.md specifies run control for three targets. Two
 * of them are emulators and can do everything. This is the third, and it is
 * the one the capability matrix exists for: no on-chip debug unit, no PSEN
 * so no MON51-style dual map, and no stcgal option bit that would let us
 * patch a trap opcode into flash. What is left is still most of a debugger,
 * because the cooperative scheduler hands it to us for free.
 *
 * WHAT THIS DEMONSTRATES, WITHOUT A BENCH
 *
 *   The two tasks below are written in exactly the shape stc_pseudocode.py
 *   emits: a `switch (<task>_state)` over numbered `case` labels, one per
 *   wait and per loop back-edge, with `bw_ms` as the millisecond clock. So
 *   "where is the program" is three variable reads — Level 1 position, §2 —
 *   and a yield breakpoint is a comparison in the dispatch loop.
 *
 *   Nothing here is specific to these two tasks. The monitor reads position
 *   through a symbol table the host loads with SYMS, so pointing it at real
 *   generated firmware is a matter of sending different addresses. The
 *   defaults installed at reset just make it self-demonstrating.
 *
 * WHAT IT COSTS
 *
 *   Timer 0  program time. Frozen while halted (§3), which is what makes a
 *            halt reversible instead of a burst of overdue ticks on resume.
 *   Timer 1  wall time. Never frozen, so `skew_ms` can report honestly how
 *            much of the world went past while the program stood still.
 *   BRT      the baud rate. The STC12's dedicated baud-rate timer, selected
 *            with AUXR.S1BRS, which is why Timer 1 is free to be the clock.
 *   UART1    P3.0/P3.1 — the same pins as ISP. They do not collide at run
 *            time (only a cold power-on enters the bootloader) but you
 *            cannot have a terminal open when you flash.
 *
 * SPDX-License-Identifier: MIT
 */
#include "board.h"
#include "live-proto.h"
#include "live-frame.h"
#include "live-sfr.h"

/* ------------------------------------------------------------------ timing
 * Timer 0 and Timer 1 both tick at 1 ms, both at FOSC/12 (AUXR.T0x12 = 0,
 * AUXR.T1x12 = 0) — the one rate a 12T and a 1T part count identically.
 */
#define TICKS_PER_MS   (FOSC_HZ / 12UL / 1000UL)
#define T_RELOAD       (65536UL - TICKS_PER_MS)
#define T_RELOAD_H     ((unsigned char)((T_RELOAD >> 8) & 0xFF))
#define T_RELOAD_L     ((unsigned char)(T_RELOAD & 0xFF))

/* ------------------------------------------------------------------- UART
 * Baud from the BRT, not from Timer 1:
 *
 *   overflow rate = FOSC / (256 - BRT)      with AUXR.BRTx12 = 1
 *   baud          = overflow rate / 32      with PCON.SMOD = 0
 *
 * At 11.0592 MHz and 115200 baud the divisor is exactly 3, which is the
 * whole reason 11.0592 MHz is the classic 8051 crystal.
 *
 * The /32 is standard UART mode 1 behaviour and is the one number here that
 * has NOT been confirmed on silicon for the BRT path specifically. If the
 * link comes up at half or double rate, that divisor is the first suspect.
 */
#ifndef LIVE_BAUD
#define LIVE_BAUD      115200UL
#endif
#define BAUD_DIV       (FOSC_HZ / (32UL * LIVE_BAUD))

#ifdef PART_STC15F2K60S2
/* Timer 2 is 16-bit, so the same divisor fits far more baud/FOSC combinations
 * than the STC12's 8-bit BRT does. */
#define T2_RELOAD      (65536UL - BAUD_DIV)
#if (BAUD_DIV < 1) || (BAUD_DIV > 65535)
#error "FOSC_HZ / LIVE_BAUD does not fit the Timer 2 reload - pick another baud"
#endif
#else
#define BRT_RELOAD     ((unsigned char)(256UL - BAUD_DIV))
#if (BAUD_DIV < 1) || (BAUD_DIV > 255)
#error "FOSC_HZ / LIVE_BAUD does not fit the BRT reload - pick another baud"
#endif
#endif

/* ------------------------------------------------------------ monitor size */
#define LIVE_NTASKS    2      /* the demo scheduler below                    */
#define LIVE_MAX_TASKS 4      /* what the symbol table can hold              */
#define LIVE_MAX_BP    4

/* ============================================================== the program
 * Two tasks in emitter shape. Deliberately not factored or tidied: the point
 * is that this is what generateC() produces, so the monitor is being tested
 * against the thing it will actually debug.
 */
static volatile unsigned int bw_ms;

static unsigned int task0_state;
static unsigned int task0_until;
static unsigned int task1_state;
static unsigned int task1_until;

static unsigned int bw_now(void)
{
    unsigned int t;
    ET0 = 0;                    /* bw_ms is 16-bit; the ISR must not split it */
    t = bw_ms;
    ET0 = 1;
    return t;
}

static void task0(void)
{
    switch (task0_state) {
    case 0:
        task0_state = 1;
    case 1:                                     /* FOREVER */
        LED1 = LED_ON;
        task0_until = bw_now() + 500;
        task0_state = 2;
    case 2:
        if ((int)(bw_now() - task0_until) < 0)
            return;
        LED1 = LED_OFF;
        task0_until = bw_now() + 500;
        task0_state = 3;
    case 3:
        if ((int)(bw_now() - task0_until) < 0)
            return;
        task0_state = 1;
        return;
    }
}

static void task1(void)
{
    switch (task1_state) {
    case 0:
        task1_state = 1;
    case 1:                                     /* FOREVER */
        LED2 = LED_ON;
        task1_until = bw_now() + 300;
        task1_state = 2;
    case 2:
        if ((int)(bw_now() - task1_until) < 0)
            return;
        LED2 = LED_OFF;
        task1_until = bw_now() + 300;
        task1_state = 3;
    case 3:
        if ((int)(bw_now() - task1_until) < 0)
            return;
        task1_state = 1;
        return;
    }
}

/* ================================================================= monitor */

/* Silence long enough to mean "the sender stopped mid-frame". Anything well
 * above one character time (87 us at 115200) and well below a human's idea
 * of a pause will do. */
#define LIVE_IDLE_MS 5

static volatile unsigned int wall_ms;         /* never frozen                */
static unsigned int skew_ms;                  /* cumulative, §7 skewNs       */
static unsigned int last_rx_ms;

static unsigned char run_state = LIVE_ST_RUNNING;
static unsigned char halt_request;
static unsigned char step_left;

static __xdata live_rx_t rx;
static __xdata unsigned char txbuf[LIVE_MAX_PAYLOAD + 4];
static __xdata unsigned char pay[LIVE_MAX_PAYLOAD];

/* The symbol table: where this firmware's position lives. Index 0 is the
 * millisecond clock; task i has its state at 1+2i and its deadline at 2+2i.
 * Loaded with defaults at reset, replaced by SYMS for foreign firmware. */
static __xdata unsigned char sym_space[1 + 2 * LIVE_MAX_TASKS];
static __xdata unsigned int  sym_addr[1 + 2 * LIVE_MAX_TASKS];
static unsigned char sym_ntasks;

static __xdata unsigned char bp_used[LIVE_MAX_BP];
static __xdata unsigned char bp_task[LIVE_MAX_BP];
static __xdata unsigned int  bp_state[LIVE_MAX_BP];

/* --------------------------------------------------------------- UART I/O */
static void uart_init(void)
{
    SCON = 0x50;                /* mode 1, 8-bit UART, receiver enabled      */

#ifdef PART_STC15F2K60S2
    /* The STC15 has no dedicated baud-rate timer: Timer 2 does that job, and
     * takes a 16-bit reload rather than the BRT's 8-bit one. The AUXR bits
     * below are bit-for-bit the same value as the STC12 case — the roles line
     * up exactly (run, 1T, select-as-UART1-source) even though the bits have
     * different names. Only the reload register differs.
     * docs/STC15-PERIPHERAL-MODEL.md §2.2. */
    T2L = (unsigned char)(T2_RELOAD & 0xFF);
    T2H = (unsigned char)((T2_RELOAD >> 8) & 0xFF);
    AUXR |= 0x15;               /* T2R=1 (run), T2x12=1 (1T), S1ST2=1        */
#else
    BRT  = BRT_RELOAD;
    AUXR |= 0x15;               /* BRTR=1 (run), BRTx12=1 (1T), S1BRS=1      */
#endif

    AUXR &= ~0xC0;              /* T0x12=0, T1x12=0 — both timers at FOSC/12 */
    TI = 0;
    RI = 0;
}

static void uart_putc(unsigned char c)
{
    SBUF = c;
    while (!TI)
        ;
    TI = 0;
}

static void uart_send(unsigned char *p, unsigned char n)
{
    unsigned char i;
    for (i = 0; i < n; i++)
        uart_putc(p[i]);
}

/* ------------------------------------------------------ memory, by space */
static unsigned char mem_read(unsigned char space, unsigned int addr,
                              unsigned char *out)
{
    __code unsigned char *cp;
    __idata unsigned char *ip;
    __xdata unsigned char *xp;
    unsigned char byte_addr;

    switch (space) {
    case LIVE_SP_CODE:
        cp = (__code unsigned char *)addr;
        *out = *cp;
        return 0;

    case LIVE_SP_IRAM:
        if (addr > 0xFF)
            return LIVE_ERR_RANGE;
        ip = (__idata unsigned char *)(unsigned char)addr;
        *out = *ip;
        return 0;

    case LIVE_SP_SFR:
        if (addr < 0x80 || addr > 0xFF)
            return LIVE_ERR_RANGE;
        return live_sfr_read((unsigned char)addr, out) ? 0 : LIVE_ERR_SPACE;

    case LIVE_SP_XRAM:
        xp = (__xdata unsigned char *)addr;
        *out = *xp;
        return 0;

    case LIVE_SP_BIT:
        /* Bits are direct-addressed too, so they get derived from the byte
         * that holds them rather than needing a second curated table. */
        if (addr > 0xFF)
            return LIVE_ERR_RANGE;
        if (addr < 0x80) {
            byte_addr = (unsigned char)(0x20 + (addr >> 3));
            ip = (__idata unsigned char *)byte_addr;
            *out = (unsigned char)((*ip >> (addr & 7)) & 1);
            return 0;
        } else {
            byte_addr = (unsigned char)(addr & 0xF8);
            if (!live_sfr_read(byte_addr, out))
                return LIVE_ERR_SPACE;
            *out = (unsigned char)((*out >> (addr & 7)) & 1);
            return 0;
        }

    default:
        return LIVE_ERR_SPACE;
    }
}

static unsigned char mem_write(unsigned char space, unsigned int addr,
                               unsigned char val)
{
    __idata unsigned char *ip;
    __xdata unsigned char *xp;
    unsigned char byte_addr, cur, err;

    switch (space) {
    case LIVE_SP_CODE:
        return LIVE_ERR_REFUSED;        /* flash is not writable from here  */

    case LIVE_SP_IRAM:
        if (addr > 0xFF)
            return LIVE_ERR_RANGE;
        ip = (__idata unsigned char *)(unsigned char)addr;
        *ip = val;
        return 0;

    case LIVE_SP_SFR:
        if (addr < 0x80 || addr > 0xFF)
            return LIVE_ERR_RANGE;
        return live_sfr_write((unsigned char)addr, val);

    case LIVE_SP_XRAM:
        xp = (__xdata unsigned char *)addr;
        *xp = val;
        return 0;

    case LIVE_SP_BIT:
        if (addr > 0xFF)
            return LIVE_ERR_RANGE;
        if (addr < 0x80) {
            byte_addr = (unsigned char)(0x20 + (addr >> 3));
            ip = (__idata unsigned char *)byte_addr;
            if (val)
                *ip = (unsigned char)(*ip | (1u << (addr & 7)));
            else
                *ip = (unsigned char)(*ip & ~(1u << (addr & 7)));
            return 0;
        } else {
            byte_addr = (unsigned char)(addr & 0xF8);
            if (!live_sfr_read(byte_addr, &cur))
                return LIVE_ERR_SPACE;
            if (val)
                cur = (unsigned char)(cur | (1u << (addr & 7)));
            else
                cur = (unsigned char)(cur & ~(1u << (addr & 7)));
            err = live_sfr_write(byte_addr, cur);
            return err;
        }

    default:
        return LIVE_ERR_SPACE;
    }
}

/* Read a 16-bit value through the symbol table. SDCC stores multi-byte
 * scalars little-endian in data memory; the wire, by contrast, is big-endian
 * throughout (see live-proto.h). Keeping the two straight is exactly the
 * kind of thing two implementations get differently. */
static unsigned int sym_read16(unsigned char idx)
{
    unsigned char lo = 0, hi = 0;
    mem_read(sym_space[idx], sym_addr[idx], &lo);
    mem_read(sym_space[idx], (unsigned int)(sym_addr[idx] + 1), &hi);
    return (unsigned int)(((unsigned int)hi << 8) | lo);
}

static void sym_defaults(void)
{
    sym_ntasks = LIVE_NTASKS;

    sym_space[0] = LIVE_SP_IRAM;  sym_addr[0] = (unsigned int)&bw_ms;
    sym_space[1] = LIVE_SP_IRAM;  sym_addr[1] = (unsigned int)&task0_state;
    sym_space[2] = LIVE_SP_IRAM;  sym_addr[2] = (unsigned int)&task0_until;
    sym_space[3] = LIVE_SP_IRAM;  sym_addr[3] = (unsigned int)&task1_state;
    sym_space[4] = LIVE_SP_IRAM;  sym_addr[4] = (unsigned int)&task1_until;
}

/* ------------------------------------------------------- position (§2 L1) */
static unsigned char pos_blob(unsigned char *p)
{
    unsigned char i, n = 0;
    unsigned int v;

    p[n++] = run_state;
    p[n++] = sym_ntasks;

    v = sym_read16(0);                          /* bw_ms */
    p[n++] = (unsigned char)(v >> 8);
    p[n++] = (unsigned char)(v & 0xFF);

    p[n++] = (unsigned char)(skew_ms >> 8);
    p[n++] = (unsigned char)(skew_ms & 0xFF);

    for (i = 0; i < sym_ntasks; i++) {
        v = sym_read16((unsigned char)(1 + 2 * i));
        p[n++] = (unsigned char)(v >> 8);
        p[n++] = (unsigned char)(v & 0xFF);
        v = sym_read16((unsigned char)(2 + 2 * i));
        p[n++] = (unsigned char)(v >> 8);
        p[n++] = (unsigned char)(v & 0xFF);
    }
    return n;
}

static void send(unsigned char cmd, unsigned char *p, unsigned char n)
{
    uart_send(txbuf, live_tx_build(txbuf, cmd, p, n));
}

static void send_nak(unsigned char cmd, unsigned char err)
{
    pay[0] = cmd;
    pay[1] = err;
    send(LIVE_NAK, pay, 2);
}

/* ------------------------------------------------------------ the commands */
static void live_dispatch(void);

static unsigned int wall_now(void)
{
    unsigned int t;
    ET1 = 0;                    /* 16-bit read must not be split by the ISR */
    t = wall_ms;
    ET1 = 1;
    return t;
}

static void live_poll(void)
{
    while (RI) {
        unsigned char b = SBUF;
        RI = 0;
        last_rx_ms = wall_now();
        if (live_rx_byte(&rx, b))
            live_dispatch();
    }

    /* Inter-frame gap: abandon anything half-received. Wall time, not
     * program time, because this must keep working while halted. */
    if (rx.st != LIVE_RX_HUNT &&
        (unsigned int)(wall_now() - last_rx_ms) >= LIVE_IDLE_MS)
        live_rx_idle(&rx);
}

static void live_halt(unsigned char cause)
{
    unsigned int wall_at_halt;
    unsigned char n;

    TR0 = 0;                        /* freeze program time — §3            */
    wall_at_halt = wall_now();
    run_state = LIVE_ST_HALTED;
    step_left = 0;
    halt_request = 0;

    pay[0] = cause;
    n = (unsigned char)(1 + pos_blob(&pay[1]));
    send(LIVE_EVT_HALT, pay, n);

    while (run_state == LIVE_ST_HALTED)
        live_poll();

    skew_ms = (unsigned int)(skew_ms + (unsigned int)(wall_now() - wall_at_halt));
    TR0 = 1;
}

static void live_soft_reset(void)
{
    void (*entry)(void) = (void (*)(void))0;
    entry();                        /* NOT an ISP entry — §9. Cold power    */
}                                   /* cycle is still the only way in.      */

static void live_dispatch(void)
{
    unsigned char cmd = rx.cmd;
    unsigned char n, i, err, space;
    unsigned int addr;

    switch (cmd) {
    case LIVE_CMD_HELLO:
        pay[0] = LIVE_PROTO_VERSION;
        pay[1] = LIVE_MAX_PAYLOAD;
        pay[2] = LIVE_SPMASK(LIVE_STEP_BLOCK);
        pay[3] = LIVE_SPMASK(LIVE_BP_YIELD);
        pay[4] = (unsigned char)(LIVE_SPMASK(LIVE_SP_CODE) |
                                 LIVE_SPMASK(LIVE_SP_IRAM) |
                                 LIVE_SPMASK(LIVE_SP_SFR)  |
                                 LIVE_SPMASK(LIVE_SP_XRAM) |
                                 LIVE_SPMASK(LIVE_SP_BIT));
        pay[5] = (unsigned char)(pay[4] & ~LIVE_SPMASK(LIVE_SP_CODE));
        pay[6] = LIVE_FLAG_TIME_FREEZES;    /* no PC, no full SFR set       */
        pay[7] = LIVE_MAX_TASKS;
        send(LIVE_REPLY(cmd), pay, 8);
        return;

    case LIVE_CMD_READ:
        if (rx.len != 4) { send_nak(cmd, LIVE_ERR_BADLEN); return; }
        space = rx.buf[0];
        addr  = (unsigned int)(((unsigned int)rx.buf[1] << 8) | rx.buf[2]);
        n     = rx.buf[3];
        if (n > LIVE_MAX_PAYLOAD) { send_nak(cmd, LIVE_ERR_RANGE); return; }
        for (i = 0; i < n; i++) {
            err = mem_read(space, (unsigned int)(addr + i), &pay[i]);
            if (err) { send_nak(cmd, err); return; }
        }
        send(LIVE_REPLY(cmd), pay, n);
        return;

    case LIVE_CMD_WRITE:
        if (rx.len < 4) { send_nak(cmd, LIVE_ERR_BADLEN); return; }
        space = rx.buf[0];
        addr  = (unsigned int)(((unsigned int)rx.buf[1] << 8) | rx.buf[2]);
        for (i = 3; i < rx.len; i++) {
            err = mem_write(space, (unsigned int)(addr + i - 3), rx.buf[i]);
            if (err) { send_nak(cmd, err); return; }
        }
        pay[0] = (unsigned char)(rx.len - 3);
        send(LIVE_REPLY(cmd), pay, 1);
        return;

    case LIVE_CMD_REGS:
        /* No PC: the only return address reachable from here is the
         * monitor's own, and the meaningful position at a yield point is
         * (task, state), not an address. HELLO says so with PC_VALID = 0. */
        pay[0] = ACC;  pay[1] = B;    pay[2] = DPL;  pay[3] = DPH;
        pay[4] = SP;   pay[5] = PSW;  pay[6] = (unsigned char)((PSW >> 3) & 3);
        for (i = 0; i < 8; i++) {
            __idata unsigned char *r =
                (__idata unsigned char *)(unsigned char)((pay[6] << 3) + i);
            pay[7 + i] = *r;
        }
        send(LIVE_REPLY(cmd), pay, 15);
        return;

    case LIVE_CMD_RUN:
        run_state = LIVE_ST_RUNNING;
        step_left = 0;
        pay[0] = run_state;
        send(LIVE_REPLY(cmd), pay, 1);
        return;

    case LIVE_CMD_HALT:
        halt_request = 1;           /* takes effect at the next yield point */
        pay[0] = run_state;
        send(LIVE_REPLY(cmd), pay, 1);
        return;

    case LIVE_CMD_STEP:
        if (rx.len != 2) { send_nak(cmd, LIVE_ERR_BADLEN); return; }
        if (rx.buf[0] != LIVE_STEP_BLOCK) {
            send_nak(cmd, LIVE_ERR_UNSUP);
            return;
        }
        step_left = rx.buf[1] ? rx.buf[1] : 1;
        run_state = LIVE_ST_RUNNING;
        pay[0] = step_left;
        send(LIVE_REPLY(cmd), pay, 1);
        return;

    case LIVE_CMD_BPSET:
        if (rx.len != 4) { send_nak(cmd, LIVE_ERR_BADLEN); return; }
        if (rx.buf[0] != LIVE_BP_YIELD) { send_nak(cmd, LIVE_ERR_UNSUP); return; }
        if (rx.buf[1] >= sym_ntasks)    { send_nak(cmd, LIVE_ERR_RANGE); return; }
        for (i = 0; i < LIVE_MAX_BP; i++) {
            if (!bp_used[i]) {
                bp_used[i]  = 1;
                bp_task[i]  = rx.buf[1];
                bp_state[i] = (unsigned int)(((unsigned int)rx.buf[2] << 8) |
                                             rx.buf[3]);
                pay[0] = i;
                send(LIVE_REPLY(cmd), pay, 1);
                return;
            }
        }
        send_nak(cmd, LIVE_ERR_RANGE);
        return;

    case LIVE_CMD_BPCLR:
        if (rx.len != 1)             { send_nak(cmd, LIVE_ERR_BADLEN); return; }
        if (rx.buf[0] >= LIVE_MAX_BP) { send_nak(cmd, LIVE_ERR_RANGE); return; }
        bp_used[rx.buf[0]] = 0;
        pay[0] = rx.buf[0];
        send(LIVE_REPLY(cmd), pay, 1);
        return;

    case LIVE_CMD_POS:
        n = pos_blob(pay);
        send(LIVE_REPLY(cmd), pay, n);
        return;

    case LIVE_CMD_SYMS:
        /* ntasks, then (space, addr_hi, addr_lo) for bw_ms and for each
         * task's state and until — 1 + 2*ntasks entries. */
        if (rx.len < 1) { send_nak(cmd, LIVE_ERR_BADLEN); return; }
        n = rx.buf[0];
        if (n > LIVE_MAX_TASKS) { send_nak(cmd, LIVE_ERR_RANGE); return; }
        if (rx.len != (unsigned char)(1 + 3 * (1 + 2 * n))) {
            send_nak(cmd, LIVE_ERR_BADLEN);
            return;
        }
        sym_ntasks = n;
        for (i = 0; i < (unsigned char)(1 + 2 * n); i++) {
            sym_space[i] = rx.buf[1 + 3 * i];
            sym_addr[i]  = (unsigned int)(((unsigned int)rx.buf[2 + 3 * i] << 8) |
                                          rx.buf[3 + 3 * i]);
        }
        pay[0] = sym_ntasks;
        send(LIVE_REPLY(cmd), pay, 1);
        return;

    case LIVE_CMD_RESET:
        pay[0] = LIVE_HALT_RESET;
        send(LIVE_REPLY(cmd), pay, 1);
        live_soft_reset();
        return;

    default:
        send_nak(cmd, LIVE_ERR_BADCMD);
        return;
    }
}

/* Why the program should stop before dispatching task `t`, or 0 to keep
 * going. This is the whole of run control on this target: no trap opcodes,
 * no flash writes, one comparison per task per pass. */
static unsigned char stop_reason(unsigned char t)
{
    unsigned char i;
    unsigned int st;

    if (halt_request)
        return LIVE_HALT_USER;

    st = sym_read16((unsigned char)(1 + 2 * t));
    for (i = 0; i < LIVE_MAX_BP; i++)
        if (bp_used[i] && bp_task[i] == t && bp_state[i] == st)
            return LIVE_HALT_BREAKPOINT;

    if (step_left && --step_left == 0)
        return LIVE_HALT_STEP;

    return 0;
}

/* ------------------------------------------------------------------- ISRs */
static void t0_isr(void) __interrupt(1)
{
    TH0 = T_RELOAD_H;
    TL0 = T_RELOAD_L;
    bw_ms++;                        /* program time — stops when TR0 does   */
}

static void t1_isr(void) __interrupt(3)
{
    TH1 = T_RELOAD_H;
    TL1 = T_RELOAD_L;
    wall_ms++;                      /* wall time — never stops              */
}

static void timers_init(void)
{
    TMOD = 0x11;                    /* Timer 0 and Timer 1 both 16-bit mode 1 */
    TH0 = T_RELOAD_H;  TL0 = T_RELOAD_L;
    TH1 = T_RELOAD_H;  TL1 = T_RELOAD_L;
    TF0 = 0;  TF1 = 0;
    ET0 = 1;  ET1 = 1;
    TR0 = 1;  TR1 = 1;
    EA = 1;
}

void main(void)
{
    unsigned char t, cause;

    board_init();
    uart_init();
    live_rx_init(&rx);
    sym_defaults();
    timers_init();

    for (;;) {
        for (t = 0; t < LIVE_NTASKS; t++) {
            live_poll();

            if (run_state == LIVE_ST_RUNNING) {
                cause = stop_reason(t);
                if (cause)
                    live_halt(cause);
            }

            if (t == 0)
                task0();
            else
                task1();
        }
    }
}
