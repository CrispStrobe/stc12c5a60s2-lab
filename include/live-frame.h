/*
 * live-frame.h — the framing codec for live-proto.h.
 *
 * Pure C: no SFRs, no SDCC storage classes, no libc. It compiles for the
 * chip with SDCC and for the host with cc, which is the point — the parser
 * that runs on silicon is the one the host tests exercise.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef LIVE_FRAME_H
#define LIVE_FRAME_H

#include "live-proto.h"

/* Receiver states. Not part of the wire format; internal to the decoder. */
#define LIVE_RX_HUNT   0
#define LIVE_RX_LEN    1
#define LIVE_RX_CMD    2
#define LIVE_RX_DATA   3
#define LIVE_RX_SUM    4

typedef struct {
    unsigned char st;
    unsigned char len;                     /* payload length              */
    unsigned char cmd;
    unsigned char n;                       /* payload bytes received      */
    unsigned char sum;                     /* running sum of LEN+CMD+data */
    unsigned char buf[LIVE_MAX_PAYLOAD];
} live_rx_t;

static void live_rx_init(live_rx_t *rx)
{
    rx->st = LIVE_RX_HUNT;
    rx->len = 0;
    rx->cmd = 0;
    rx->n = 0;
    rx->sum = 0;
}

/*
 * Abandon a partially received frame.
 *
 * Unescaped framing has one failure it cannot recover from on its own: a
 * *truncated* frame leaves the decoder mid-payload, so it eats the leading
 * bytes of the next frame as payload and loses that one too. Checksums do
 * not help — the damage is to the framing, not the contents.
 *
 * The fix is the one every byte-oriented link uses: treat a gap in the byte
 * stream as a frame boundary. The caller owns the clock, so the caller makes
 * that call; this header stays free of hardware. The firmware calls it from
 * its poll loop after LIVE_IDLE_MS of silence, the host after a read timeout.
 *
 * Without it the link still works and still cannot be fooled into accepting
 * a bad frame — it just loses one extra frame after any truncation.
 */
static void live_rx_idle(live_rx_t *rx)
{
    rx->st = LIVE_RX_HUNT;
    rx->n = 0;
}

/*
 * Feed one received byte. Returns 1 exactly when rx->cmd / rx->len / rx->buf
 * hold a complete frame with a good checksum, 0 otherwise.
 *
 * Every failure path returns to HUNT rather than trying to be clever. A
 * corrupt frame costs one frame, and the next 0x7E resynchronises.
 */
static unsigned char live_rx_byte(live_rx_t *rx, unsigned char b)
{
    switch (rx->st) {
    case LIVE_RX_HUNT:
        if (b == LIVE_SOF)
            rx->st = LIVE_RX_LEN;
        return 0;

    case LIVE_RX_LEN:
        if (b > LIVE_MAX_PAYLOAD) {
            /* Not a length we could ever accept, so this was not a real SOF.
             * Hunt again — and do it from THIS byte, in case it is one. */
            rx->st = (b == LIVE_SOF) ? LIVE_RX_LEN : LIVE_RX_HUNT;
            return 0;
        }
        rx->len = b;
        rx->sum = b;
        rx->n = 0;
        rx->st = LIVE_RX_CMD;
        return 0;

    case LIVE_RX_CMD:
        rx->cmd = b;
        rx->sum = (unsigned char)(rx->sum + b);
        rx->st = rx->len ? LIVE_RX_DATA : LIVE_RX_SUM;
        return 0;

    case LIVE_RX_DATA:
        rx->buf[rx->n++] = b;
        rx->sum = (unsigned char)(rx->sum + b);
        if (rx->n == rx->len)
            rx->st = LIVE_RX_SUM;
        return 0;

    default:                                /* LIVE_RX_SUM */
        rx->st = LIVE_RX_HUNT;
        return (unsigned char)(((unsigned char)(rx->sum + b)) == 0);
    }
}

/*
 * Build a frame into `out`, which must have room for n + 4 bytes.
 * Returns the total length written.
 */
static unsigned char live_tx_build(unsigned char *out, unsigned char cmd,
                                   const unsigned char *p, unsigned char n)
{
    unsigned char i;
    unsigned char s;

    out[0] = LIVE_SOF;
    out[1] = n;
    out[2] = cmd;
    s = (unsigned char)(n + cmd);

    for (i = 0; i < n; i++) {
        out[3 + i] = p[i];
        s = (unsigned char)(s + p[i]);
    }

    out[3 + n] = (unsigned char)(0u - s);
    return (unsigned char)(n + 4);
}

#endif /* LIVE_FRAME_H */
