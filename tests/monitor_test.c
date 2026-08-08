/*
 * monitor_test.c — the debug monitor's command layer, driven on the host.
 *
 * include/live-monitor.h is the whole of boundary D's on-chip behaviour with
 * the hardware lifted out behind five calls. This file supplies fakes for
 * those five and drives the real dispatch through the real frame decoder, so
 * what runs here is byte-for-byte what runs on the chip between "a frame
 * arrived" and "a reply went out".
 *
 * Why bother, given there is an emulator: neither emulator models a UART, and
 * the one STC15 is not available. This is the only thing standing between a
 * protocol bug and a wasted bench session.
 *
 *   make test
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

/* Four tasks and four breakpoints, as on the chip. LIVE_MEM stays empty: a
 * host has no __xdata. */
#define LIVE_MAX_TASKS 4
#define LIVE_MAX_BP    4

#include "live-monitor.h"

static int checks, failures;

static void ok(int cond, const char *what)
{
    checks++;
    if (!cond) {
        failures++;
        printf("  FAIL %s\n", what);
    }
}

/* ------------------------------------------------------------- the fakes */

#define FAKE_SIZE 512
static unsigned char fake_iram[256];
static unsigned char fake_xram[FAKE_SIZE];
static unsigned char fake_sfr[256];
static unsigned char fake_code[FAKE_SIZE];
static int reset_calls;

/* Mirrors the real one's contract: code is readable and never writable, and an
 * unknown space is refused rather than silently treated as one of the others. */
static unsigned char live_mem_read(unsigned char space, unsigned int addr,
                            unsigned char *out)
{
    switch (space) {
    case LIVE_SP_CODE:
        if (addr >= FAKE_SIZE) return LIVE_ERR_RANGE;
        *out = fake_code[addr];  return 0;
    case LIVE_SP_IRAM:
        if (addr > 0xFF) return LIVE_ERR_RANGE;
        *out = fake_iram[addr];  return 0;
    case LIVE_SP_SFR:
        if (addr < 0x80 || addr > 0xFF) return LIVE_ERR_RANGE;
        *out = fake_sfr[addr];   return 0;
    case LIVE_SP_XRAM:
        if (addr >= FAKE_SIZE) return LIVE_ERR_RANGE;
        *out = fake_xram[addr];  return 0;
    default:
        return LIVE_ERR_SPACE;
    }
}

static unsigned char live_mem_write(unsigned char space, unsigned int addr,
                             unsigned char val)
{
    switch (space) {
    case LIVE_SP_CODE:
        return LIVE_ERR_REFUSED;             /* flash is not writable        */
    case LIVE_SP_IRAM:
        if (addr > 0xFF) return LIVE_ERR_RANGE;
        fake_iram[addr] = val;  return 0;
    case LIVE_SP_SFR:
        if (addr == 0x98) return LIVE_ERR_REFUSED;   /* SCON — the link      */
        if (addr < 0x80 || addr > 0xFF) return LIVE_ERR_RANGE;
        fake_sfr[addr] = val;   return 0;
    case LIVE_SP_XRAM:
        if (addr >= FAKE_SIZE) return LIVE_ERR_RANGE;
        fake_xram[addr] = val;  return 0;
    default:
        return LIVE_ERR_SPACE;
    }
}

static unsigned char live_regs_blob(unsigned char *p)
{
    unsigned char i;
    for (i = 0; i < 15; i++)
        p[i] = (unsigned char)(0xA0 + i);
    return 15;
}

static void live_reset_target(void) { reset_calls++; }

/* What the monitor sent, decoded back into frames. */
static unsigned char out_cmd;
static unsigned char out_len;
static unsigned char out_buf[LIVE_MAX_PAYLOAD];
static int out_frames;

static void live_uart_send(unsigned char *p, unsigned char n)
{
    static live_rx_t dec;
    unsigned char i;
    static int inited;
    if (!inited) { live_rx_init(&dec); inited = 1; }
    for (i = 0; i < n; i++)
        if (live_rx_byte(&dec, p[i])) {
            out_cmd = dec.cmd;
            out_len = dec.len;
            memcpy(out_buf, dec.buf, dec.len);
            out_frames++;
        }
}

/* ---------------------------------------------------------------- driver */

/* Feed a command in through the real decoder, exactly as live_poll() does. */
static void request(unsigned char cmd, const unsigned char *payload, unsigned char n)
{
    unsigned char frame[LIVE_MAX_PAYLOAD + 4];
    unsigned char total, i;

    out_frames = 0;
    out_cmd = 0;
    total = live_tx_build(frame, cmd, payload, n);
    for (i = 0; i < total; i++)
        if (live_rx_byte(&rx, frame[i]))
            live_dispatch();
}

static int replied(unsigned char cmd)   { return out_frames == 1 && out_cmd == LIVE_REPLY(cmd); }
static int naked(unsigned char cmd, unsigned char err)
{
    return out_frames == 1 && out_cmd == LIVE_NAK &&
           out_len == 2 && out_buf[0] == cmd && out_buf[1] == err;
}

/* ----------------------------------------------------------------- tests */

static void test_hello(void)
{
    printf("HELLO reports the on-chip row of the capability matrix, honestly\n");
    request(LIVE_CMD_HELLO, NULL, 0);
    ok(replied(LIVE_CMD_HELLO), "HELLO is answered");
    ok(out_len == 9, "blob is 9 bytes");
    ok(out_buf[0] == LIVE_PROTO_VERSION, "protocol version");
    ok(out_buf[2] == (1u << LIVE_STEP_BLOCK), "block stepping ONLY - no insn, no line");
    ok(out_buf[3] == (1u << LIVE_BP_YIELD), "yield breakpoints ONLY - no code, no watch");
    ok((out_buf[4] & (1u << LIVE_SP_CODE)) != 0, "code is readable");
    ok((out_buf[5] & (1u << LIVE_SP_CODE)) == 0, "code is NOT writable");
    ok((out_buf[6] & LIVE_FLAG_TIME_FREEZES) != 0, "halting freezes program time");
    ok((out_buf[6] & LIVE_FLAG_PC_VALID) == 0, "no PC is claimed");
    ok((out_buf[6] & LIVE_FLAG_SFRS_ALL) == 0, "SFR window is curated, not all 256");

    /* A debugger is not free on a part this size. Saying what it took lets a
     * front end explain a dead feature instead of just showing one -- the
     * case that forced it being TONE, which is Timer 1, which is also the
     * wall clock behind skewNs. */
    ok(out_len == 9, "the blob carries the consumed-peripherals byte");
    ok((out_buf[8] & LIVE_RES_TIMER1) != 0,
       "Timer 1 is declared taken, which is why a TONE pin cannot work here");
    ok((out_buf[8] & LIVE_RES_UART1) != 0, "and UART1, which is also the ISP pins");
    ok((out_buf[8] & LIVE_RES_PCA) == 0,
       "but NOT the PCA -- so PWM still works under the monitor");
}

static void test_read_write(void)
{
    unsigned char p[8];

    printf("READ and WRITE across spaces, and the refusals\n");

    fake_xram[0x40] = 0xDE; fake_xram[0x41] = 0xAD;
    p[0] = LIVE_SP_XRAM; p[1] = 0x00; p[2] = 0x40; p[3] = 2;
    request(LIVE_CMD_READ, p, 4);
    ok(replied(LIVE_CMD_READ) && out_len == 2 &&
       out_buf[0] == 0xDE && out_buf[1] == 0xAD, "reads xram, big-endian address");

    p[0] = LIVE_SP_XRAM; p[1] = 0x00; p[2] = 0x50; p[3] = 0xBE; p[4] = 0xEF;
    request(LIVE_CMD_WRITE, p, 5);
    ok(replied(LIVE_CMD_WRITE) && out_buf[0] == 2, "write reports 2 bytes written");
    ok(fake_xram[0x50] == 0xBE && fake_xram[0x51] == 0xEF, "and they landed");

    p[0] = LIVE_SP_CODE; p[1] = 0x00; p[2] = 0x10; p[3] = 0x01;
    request(LIVE_CMD_WRITE, p, 4);
    ok(naked(LIVE_CMD_WRITE, LIVE_ERR_REFUSED), "writing code is refused, with a reason");

    p[0] = LIVE_SP_SFR; p[1] = 0x00; p[2] = 0x98; p[3] = 0x00;
    request(LIVE_CMD_WRITE, p, 4);
    ok(naked(LIVE_CMD_WRITE, LIVE_ERR_REFUSED), "writing SCON is refused - it IS the link");

    p[0] = 99; p[1] = 0; p[2] = 0; p[3] = 1;
    request(LIVE_CMD_READ, p, 4);
    ok(naked(LIVE_CMD_READ, LIVE_ERR_SPACE), "unknown space is refused, not guessed");

    p[0] = LIVE_SP_XRAM; p[1] = 0; p[2] = 0; p[3] = LIVE_MAX_PAYLOAD + 1;
    request(LIVE_CMD_READ, p, 4);
    ok(naked(LIVE_CMD_READ, LIVE_ERR_RANGE), "over-long read is refused");

    request(LIVE_CMD_READ, p, 3);
    ok(naked(LIVE_CMD_READ, LIVE_ERR_BADLEN), "wrong payload length is refused");

    /* A read that fails partway must NAK, not return a short frame of
     * whatever it managed — a truncated reply would look like valid data. */
    p[0] = LIVE_SP_IRAM; p[1] = 0x00; p[2] = 0xFE; p[3] = 8;
    request(LIVE_CMD_READ, p, 4);
    ok(naked(LIVE_CMD_READ, LIVE_ERR_RANGE), "a read running off the end NAKs, not truncates");
}

static void test_symbols_and_position(void)
{
    unsigned char p[32];
    unsigned char n = 0;

    printf("SYMS then POS - Level 1 position, the whole point of the thing\n");

    /* bw_ms at iram 0x08, task0 state/until at 0x0A/0x0C, task1 at 0x0E/0x10 —
     * the layout a real build actually produces. Little-endian in memory,
     * big-endian on the wire; getting that backwards is the classic bug. */
    fake_iram[0x08] = 0x2C; fake_iram[0x09] = 0x01;      /* bw_ms  = 300 */
    fake_iram[0x0A] = 0x03; fake_iram[0x0B] = 0x00;      /* t0 state = 3 */
    fake_iram[0x0C] = 0x2C; fake_iram[0x0D] = 0x01;      /* t0 until = 300 */
    fake_iram[0x0E] = 0xFF; fake_iram[0x0F] = 0xFF;      /* t1 state = done */
    fake_iram[0x10] = 0x00; fake_iram[0x11] = 0x00;

    p[n++] = 2;                                          /* ntasks */
    p[n++] = LIVE_SP_IRAM; p[n++] = 0; p[n++] = 0x08;
    p[n++] = LIVE_SP_IRAM; p[n++] = 0; p[n++] = 0x0A;
    p[n++] = LIVE_SP_IRAM; p[n++] = 0; p[n++] = 0x0C;
    p[n++] = LIVE_SP_IRAM; p[n++] = 0; p[n++] = 0x0E;
    p[n++] = LIVE_SP_IRAM; p[n++] = 0; p[n++] = 0x10;
    request(LIVE_CMD_SYMS, p, n);
    ok(replied(LIVE_CMD_SYMS) && out_buf[0] == 2, "SYMS accepts 2 tasks");

    request(LIVE_CMD_POS, NULL, 0);
    ok(replied(LIVE_CMD_POS), "POS is answered");
    ok(out_len == 6 + 4 * 2, "blob is 6 + 4 per task");
    ok(out_buf[1] == 2, "ntasks");
    ok(out_buf[2] == 0x01 && out_buf[3] == 0x2C, "bw_ms is 300, big-endian on the wire");
    ok(out_buf[6] == 0x00 && out_buf[7] == 0x03, "task0 state 3");
    ok(out_buf[8] == 0x01 && out_buf[9] == 0x2C, "task0 until 300");
    ok(out_buf[10] == 0xFF && out_buf[11] == 0xFF, "task1 reports the 0xFFFF done sentinel");

    /* Too many tasks, and a length that does not match the declared count. */
    p[0] = LIVE_MAX_TASKS + 1;
    request(LIVE_CMD_SYMS, p, n);
    ok(naked(LIVE_CMD_SYMS, LIVE_ERR_RANGE), "more tasks than the table holds is refused");
    p[0] = 2;
    request(LIVE_CMD_SYMS, p, (unsigned char)(n - 1));
    ok(naked(LIVE_CMD_SYMS, LIVE_ERR_BADLEN), "a truncated symbol table is refused");
}

static void test_breakpoints_and_stepping(void)
{
    unsigned char p[4];
    unsigned char h[LIVE_MAX_BP];
    int i;

    printf("yield breakpoints, and the step kinds this target does not have\n");

    p[0] = LIVE_BP_CODE; p[1] = 0; p[2] = 0; p[3] = 1;
    request(LIVE_CMD_BPSET, p, 4);
    ok(naked(LIVE_CMD_BPSET, LIVE_ERR_UNSUP),
       "a CODE breakpoint is refused - no PSEN, no IAP route (model 5)");

    p[0] = LIVE_BP_YIELD; p[1] = 99; p[2] = 0; p[3] = 1;
    request(LIVE_CMD_BPSET, p, 4);
    ok(naked(LIVE_CMD_BPSET, LIVE_ERR_RANGE), "a breakpoint on a task that does not exist");

    for (i = 0; i < LIVE_MAX_BP; i++) {
        p[0] = LIVE_BP_YIELD; p[1] = 0; p[2] = 0; p[3] = (unsigned char)(i + 1);
        request(LIVE_CMD_BPSET, p, 4);
        ok(replied(LIVE_CMD_BPSET), "breakpoint allocated");
        h[i] = out_buf[0];
    }
    p[0] = LIVE_BP_YIELD; p[1] = 0; p[2] = 0; p[3] = 9;
    request(LIVE_CMD_BPSET, p, 4);
    ok(naked(LIVE_CMD_BPSET, LIVE_ERR_RANGE), "a full breakpoint table is refused, not silently dropped");

    /* task0_state is 3 in the fake memory; a breakpoint on (task 0, state 3)
     * must fire, and one on another state must not. */
    for (i = 0; i < LIVE_MAX_BP; i++) {
        p[0] = h[i];
        request(LIVE_CMD_BPCLR, p, 1);
        ok(replied(LIVE_CMD_BPCLR), "breakpoint cleared");
    }
    ok(stop_reason(0) == 0, "nothing stops a running program by default");

    p[0] = LIVE_BP_YIELD; p[1] = 0; p[2] = 0; p[3] = 3;
    request(LIVE_CMD_BPSET, p, 4);
    ok(stop_reason(0) == LIVE_HALT_BREAKPOINT, "breakpoint on (task 0, state 3) fires");
    ok(stop_reason(1) == 0, "and does not fire for task 1");
    p[0] = out_buf[0];
    request(LIVE_CMD_BPCLR, p, 1);
    ok(stop_reason(0) == 0, "and stops firing once cleared");

    printf("stepping\n");
    p[0] = LIVE_STEP_INSN; p[1] = 1;
    request(LIVE_CMD_STEP, p, 2);
    ok(naked(LIVE_CMD_STEP, LIVE_ERR_UNSUP),
       "instruction stepping is refused - it needs the INT0 trick (model 4.1)");

    p[0] = LIVE_STEP_BLOCK; p[1] = 2;
    request(LIVE_CMD_STEP, p, 2);
    ok(replied(LIVE_CMD_STEP) && run_state == LIVE_ST_RUNNING, "block step resumes");
    ok(stop_reason(0) == 0, "first yield point passes");
    ok(stop_reason(0) == LIVE_HALT_STEP, "second yield point halts - step of 2 means 2");

    halt_request = 1;
    ok(stop_reason(0) == LIVE_HALT_USER, "a halt request wins immediately");
    halt_request = 0;
}

static void test_control(void)
{
    unsigned char p[2];

    printf("run control and the odds and ends\n");

    request(LIVE_CMD_RUN, NULL, 0);
    ok(replied(LIVE_CMD_RUN) && run_state == LIVE_ST_RUNNING, "RUN resumes");

    request(LIVE_CMD_HALT, NULL, 0);
    ok(replied(LIVE_CMD_HALT) && halt_request == 1,
       "HALT is a REQUEST - it takes effect at the next yield point, not now");
    halt_request = 0;

    request(LIVE_CMD_REGS, NULL, 0);
    ok(replied(LIVE_CMD_REGS) && out_len == 15 && out_buf[0] == 0xA0, "REGS blob");

    reset_calls = 0;
    request(LIVE_CMD_RESET, NULL, 0);
    ok(out_frames == 1 && reset_calls == 1,
       "RESET replies BEFORE resetting - the other order loses the acknowledgement");

    p[0] = 0;
    request(0x7A, p, 1);
    ok(naked(0x7A, LIVE_ERR_BADCMD), "an unknown command is NAKed, not ignored");
}

/* The poll loop's recovery path, at the command level rather than the codec
 * level: a host that dies mid-frame must not wedge the monitor. */
static void test_truncated_command(void)
{
    unsigned char frame[LIVE_MAX_PAYLOAD + 4];
    unsigned char total, i;

    printf("a host that dies mid-command does not wedge the monitor\n");

    total = live_tx_build(frame, LIVE_CMD_HELLO, NULL, 0);
    out_frames = 0;
    for (i = 0; i < total - 1; i++)                  /* everything but the sum */
        if (live_rx_byte(&rx, frame[i]))
            live_dispatch();
    ok(out_frames == 0, "a truncated command produces no reply");

    /* Without the idle gap the next command is eaten as the tail of this one
     * (frame_test records that as a property of unescaped framing). This is
     * what live_poll() calls after LIVE_IDLE_MS of silence. */
    live_rx_idle(&rx);

    request(LIVE_CMD_HELLO, NULL, 0);
    ok(replied(LIVE_CMD_HELLO), "and after the idle gap the next one is answered");
}

int main(void)
{
    live_rx_init(&rx);

    test_hello();
    test_read_write();
    test_symbols_and_position();
    test_breakpoints_and_stepping();
    test_control();
    test_truncated_command();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
