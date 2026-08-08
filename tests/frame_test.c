/*
 * frame_test.c — the framing codec, tested on the host.
 *
 * include/live-frame.h is written in plain C with no SFRs and no SDCC
 * storage classes precisely so that this is possible: the parser exercised
 * here is byte-for-byte the one that runs on the chip. A framing bug found
 * on a laptop costs a rebuild; the same bug found on silicon costs a bench
 * session and looks like a hardware fault.
 *
 *   make test
 *
 * With --vectors it also prints encoder test vectors, which tools/live-
 * monitor.py checks its own independent Python encoder against. Two
 * implementations of one wire format is the only way to find out whether
 * the format is actually written down clearly enough.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "live-frame.h"

static int failures;
static int checks;

static void ok(int cond, const char *what)
{
    checks++;
    if (!cond) {
        failures++;
        printf("  FAIL %s\n", what);
    }
}

/* Feed a buffer to a decoder; return how many complete frames came out. */
static int feed(live_rx_t *rx, const unsigned char *p, int n)
{
    int i, frames = 0;
    for (i = 0; i < n; i++)
        if (live_rx_byte(rx, p[i]))
            frames++;
    return frames;
}

static void test_roundtrip(void)
{
    unsigned char frame[LIVE_MAX_PAYLOAD + 4];
    unsigned char payload[LIVE_MAX_PAYLOAD];
    live_rx_t rx;
    int n, i, total, sum;

    printf("round trip, every payload length 0..%d\n", LIVE_MAX_PAYLOAD);

    for (n = 0; n <= LIVE_MAX_PAYLOAD; n++) {
        for (i = 0; i < n; i++)
            payload[i] = (unsigned char)(i * 7 + n);

        total = live_tx_build(frame, LIVE_CMD_READ, payload, (unsigned char)n);
        ok(total == n + 4, "frame length is payload + 4");

        /* The checksum property the format promises. */
        sum = 0;
        for (i = 1; i < total; i++)
            sum += frame[i];
        ok((sum & 0xFF) == 0, "LEN+CMD+payload+SUM is zero mod 256");

        live_rx_init(&rx);
        /* Nothing may complete before the final byte. */
        ok(feed(&rx, frame, total - 1) == 0, "no early completion");
        ok(live_rx_byte(&rx, frame[total - 1]) == 1, "completes on last byte");
        ok(rx.cmd == LIVE_CMD_READ, "command survives");
        ok(rx.len == n, "length survives");
        ok(memcmp(rx.buf, payload, (size_t)n) == 0, "payload survives");
    }
}

static void test_corruption(void)
{
    unsigned char frame[LIVE_MAX_PAYLOAD + 4];
    unsigned char payload[8];
    live_rx_t rx;
    int total, i;

    printf("corruption is rejected, not accepted quietly\n");

    for (i = 0; i < 8; i++)
        payload[i] = (unsigned char)(0x10 + i);
    total = live_tx_build(frame, LIVE_CMD_WRITE, payload, 8);

    /* Flip a payload byte. */
    frame[5] ^= 0x01;
    live_rx_init(&rx);
    ok(feed(&rx, frame, total) == 0, "bad checksum yields no frame");
    frame[5] ^= 0x01;

    /* Flip the checksum itself. */
    frame[total - 1] ^= 0x80;
    live_rx_init(&rx);
    ok(feed(&rx, frame, total) == 0, "corrupt checksum yields no frame");
    frame[total - 1] ^= 0x80;

    /* And the intact frame still decodes, so the tests above meant something. */
    live_rx_init(&rx);
    ok(feed(&rx, frame, total) == 1, "intact frame still decodes");
}

static void test_resync(void)
{
    unsigned char frame[LIVE_MAX_PAYLOAD + 4];
    unsigned char payload[4] = {1, 2, 3, 4};
    unsigned char junk[] = {0x00, 0xFF, 0xAA, 0x7E, 0xFF, 0x55, 0x7E};
    live_rx_t rx;
    int total;

    printf("resynchronisation after garbage\n");

    total = live_tx_build(frame, LIVE_CMD_POS, payload, 4);

    /* Garbage first — including false SOFs and an impossible length. */
    live_rx_init(&rx);
    ok(feed(&rx, junk, (int)sizeof(junk)) == 0, "garbage produces no frames");
    ok(feed(&rx, frame, total) == 1, "good frame decodes after garbage");

    /* A truncated frame, then a good one.
     *
     * This is the one case unescaped framing cannot fix by itself: the
     * decoder is stranded mid-payload, so it swallows the next frame's
     * leading bytes as payload and loses that frame too. Recorded here as a
     * property of the format rather than hidden, because a reader who does
     * not know it will misread a dropped command as a firmware fault. */
    live_rx_init(&rx);
    ok(feed(&rx, frame, total - 2) == 0, "truncated frame produces nothing");
    ok(feed(&rx, frame, total) == 0, "truncation eats the NEXT frame too");
    ok(feed(&rx, frame, total) == 1, "and the one after that decodes");

    /* Which is exactly what the idle timeout is for. */
    live_rx_init(&rx);
    ok(feed(&rx, frame, total - 2) == 0, "truncated frame produces nothing");
    live_rx_idle(&rx);
    ok(feed(&rx, frame, total) == 1, "after an idle gap, no frame is lost");
}

static void test_oversize_len(void)
{
    unsigned char bad[] = {LIVE_SOF, 0xFF, 0x01, 0x02, 0x03};
    unsigned char frame[LIVE_MAX_PAYLOAD + 4];
    unsigned char payload[2] = {0xAB, 0xCD};
    live_rx_t rx;
    int total;

    printf("an impossible length cannot overflow the buffer\n");

    live_rx_init(&rx);
    ok(feed(&rx, bad, (int)sizeof(bad)) == 0, "oversize LEN yields no frame");
    ok(rx.n <= LIVE_MAX_PAYLOAD, "receive index stayed in bounds");

    total = live_tx_build(frame, LIVE_CMD_HELLO, payload, 2);
    ok(feed(&rx, frame, total) == 1, "decoder recovered");
}

static void test_sof_in_payload(void)
{
    unsigned char frame[LIVE_MAX_PAYLOAD + 4];
    unsigned char payload[5];
    live_rx_t rx;
    int total, i;

    printf("a payload full of SOF bytes still decodes (no escaping needed)\n");

    for (i = 0; i < 5; i++)
        payload[i] = LIVE_SOF;
    total = live_tx_build(frame, LIVE_CMD_WRITE, payload, 5);

    live_rx_init(&rx);
    ok(feed(&rx, frame, total) == 1, "frame with 0x7E payload decodes");
    ok(memcmp(rx.buf, payload, 5) == 0, "payload bytes are unchanged");
}

/* Encoder vectors for tools/live-monitor.py to check its Python encoder
 * against. Format: <cmd-hex> <payload-hex-or-'-'> <frame-hex> */
static void print_vectors(void)
{
    struct { unsigned char cmd; unsigned char n; unsigned char p[8]; } v[] = {
        {LIVE_CMD_HELLO, 0, {0}},
        {LIVE_CMD_RUN,   0, {0}},
        {LIVE_CMD_READ,  4, {LIVE_SP_IRAM, 0x00, 0x08, 0x02}},
        {LIVE_CMD_WRITE, 5, {LIVE_SP_XRAM, 0x01, 0x23, 0xDE, 0xAD}},
        {LIVE_CMD_BPSET, 4, {LIVE_BP_YIELD, 0x01, 0x00, 0x03}},
        {LIVE_CMD_STEP,  2, {LIVE_STEP_BLOCK, 0x01}},
        {LIVE_CMD_WRITE, 3, {LIVE_SP_SFR, 0x00, 0x90}},
        {LIVE_EVT_HALT,  6, {LIVE_HALT_BREAKPOINT, 0x01, 0x02, 0x00, 0x00, 0x7E}},
    };
    unsigned char frame[LIVE_MAX_PAYLOAD + 4];
    int i, j, total;

    for (i = 0; i < (int)(sizeof(v) / sizeof(v[0])); i++) {
        total = live_tx_build(frame, v[i].cmd, v[i].p, v[i].n);
        printf("%02X ", v[i].cmd);
        if (v[i].n == 0)
            printf("- ");
        else {
            for (j = 0; j < v[i].n; j++)
                printf("%02X", v[i].p[j]);
            printf(" ");
        }
        for (j = 0; j < total; j++)
            printf("%02X", frame[j]);
        printf("\n");
    }
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--vectors") == 0) {
        print_vectors();
        return 0;
    }

    test_roundtrip();
    test_corruption();
    test_resync();
    test_oversize_len();
    test_sof_in_payload();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
