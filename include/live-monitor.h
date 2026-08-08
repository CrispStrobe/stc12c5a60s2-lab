/*
 * live-monitor.h — the command layer of boundary D, with no hardware in it.
 *
 * live-frame.h made the FRAMING testable on a host by keeping SFRs out of it.
 * This is the same trick one layer up: everything between "a valid frame
 * arrived" and "a reply was handed back" — the command dispatch, the refusals,
 * the Level 1 position blob, the symbol table, the breakpoint table and the
 * stop decision — is ordinary C over ordinary memory.
 *
 * It reaches hardware through exactly five calls, which the includer defines.
 * On the chip they are SFR accesses; in tests/monitor_test.c they are a fake
 * memory. So the dispatch logic that will eventually be trusted to debug real
 * silicon is exercised on every `make test`, which matters more than usual
 * here: bench time is scarce, and a protocol bug found on the bench costs a
 * session, while the same bug found on a laptop costs a rebuild.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef LIVE_MONITOR_H
#define LIVE_MONITOR_H

#include "live-proto.h"
#include "live-frame.h"

/* SDCC needs a storage class these buffers do not fit in `data`; a host
 * compiler has no such concept. */
#ifndef LIVE_MEM
#define LIVE_MEM
#endif

#ifndef LIVE_MAX_TASKS
#define LIVE_MAX_TASKS 4
#endif
#ifndef LIVE_MAX_BP
#define LIVE_MAX_BP    4
#endif

/* ---------------------------------------------------- the five hardware seams
 * Declared here, defined by whoever includes this. `static`, because there is
 * exactly one target per build; a struct of function pointers would cost code
 * and indirection on an 8051 and buy nothing, since nothing here is
 * polymorphic at run time. Leaving one undefined is a compile error, which is
 * the point.
 *
 * The split costs 154 bytes of flash — 6578 to 6732 — almost all of it
 * live_regs_blob becoming a real call instead of inline switch code. That is
 * 0.25% of the part, in exchange for the command layer being covered by
 * tests/monitor_test.c on every build. Worth it while the only STC15 is
 * unavailable and neither emulator models a UART.
 */
static unsigned char live_mem_read(unsigned char space, unsigned int addr,
                                   unsigned char *out);
static unsigned char live_mem_write(unsigned char space, unsigned int addr,
                                    unsigned char val);
static void          live_uart_send(unsigned char *p, unsigned char n);

/* Fill p with the register blob and return its length. Separate from
 * live_mem_read because the 8051's registers are not in any address space —
 * and because this target reports no PC (DEBUG-CONTROL-MODEL.md §7). */
static unsigned char live_regs_blob(unsigned char *p);

/* Restart into user code. NOT an ISP entry — only a cold power-on does that. */
static void          live_reset_target(void);

/* ------------------------------------------------------------------- state */
static unsigned char run_state = LIVE_ST_RUNNING;
static unsigned char halt_request;
static unsigned char step_left;
static unsigned int  skew_ms;                 /* cumulative, §7 skewNs        */

static LIVE_MEM live_rx_t rx;
static LIVE_MEM unsigned char txbuf[LIVE_MAX_PAYLOAD + 4];
static LIVE_MEM unsigned char pay[LIVE_MAX_PAYLOAD];

/* Where this firmware's position lives: index 0 is the millisecond clock,
 * task i has its state at 1+2i and its deadline at 2+2i. */
static LIVE_MEM unsigned char sym_space[1 + 2 * LIVE_MAX_TASKS];
static LIVE_MEM unsigned int  sym_addr[1 + 2 * LIVE_MAX_TASKS];
static unsigned char sym_ntasks;

static LIVE_MEM unsigned char bp_used[LIVE_MAX_BP];
static LIVE_MEM unsigned char bp_task[LIVE_MAX_BP];
static LIVE_MEM unsigned int  bp_state[LIVE_MAX_BP];

static unsigned int sym_read16(unsigned char idx)
{
    unsigned char lo = 0, hi = 0;
    live_mem_read(sym_space[idx], sym_addr[idx], &lo);
    live_mem_read(sym_space[idx], (unsigned int)(sym_addr[idx] + 1), &hi);
    return (unsigned int)(((unsigned int)hi << 8) | lo);
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
    live_uart_send(txbuf, live_tx_build(txbuf, cmd, p, n));
}

static void send_nak(unsigned char cmd, unsigned char err)
{
    pay[0] = cmd;
    pay[1] = err;
    send(LIVE_NAK, pay, 2);
}

/* ------------------------------------------------------------ the commands */


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
            err = live_mem_read(space, (unsigned int)(addr + i), &pay[i]);
            if (err) { send_nak(cmd, err); return; }
        }
        send(LIVE_REPLY(cmd), pay, n);
        return;

    case LIVE_CMD_WRITE:
        if (rx.len < 4) { send_nak(cmd, LIVE_ERR_BADLEN); return; }
        space = rx.buf[0];
        addr  = (unsigned int)(((unsigned int)rx.buf[1] << 8) | rx.buf[2]);
        for (i = 3; i < rx.len; i++) {
            err = live_mem_write(space, (unsigned int)(addr + i - 3), rx.buf[i]);
            if (err) { send_nak(cmd, err); return; }
        }
        pay[0] = (unsigned char)(rx.len - 3);
        send(LIVE_REPLY(cmd), pay, 1);
        return;

    case LIVE_CMD_REGS:
        send(LIVE_REPLY(cmd), pay, live_regs_blob(pay));
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
        live_reset_target();
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


#endif /* LIVE_MONITOR_H */
