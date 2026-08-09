/*
 * 20-ledcube/main.c -- 4x4x4 two-color LED cube driver
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 CrispStrobe
 *
 * Cleanroom implementation.
 *
 *   Spec:    ucsim-stc/spec-updates/008-ledcube-hardware-spec.md
 *            (hardware description written by the measurement agent)
 *
 *   Permitted reads:
 *     - The spec above.
 *     - src/20-ledcube/README.md (our own measurements: scan order,
 *       timing, port sequences — facts, not expression).
 *     - stc12.h, the STC12C5A60S2 datasheet.
 *
 *   Forbidden reads (cleanroom boundary):
 *     - icstation 4681.zip firmware sources (any file).
 *     - rgm3/ledcube444 (ledcube444.c, any derivative).
 *     - Anything under stc-research/.
 *     - bw-cfront/sb3-creator/corpus/.
 *     - The hex files from either build (compiled expression).
 *
 *   The agent that wrote the spec read the vendor firmware under an
 *   emulator; the agent that wrote this code did not. That separation
 *   is the cleanroom claim.
 *
 * Timing:
 *   Timer 0 at FOSC/12, polled (not ISR). A 12T and a 1T part
 *   count this identically — avoids the software-delay 1T/12T trap.
 *
 * Hardware:
 *   STC12C5A60S2, FOSC = 11.0592 MHz, 1T core.
 *   P0[7:0] = column cathode data (active-low).
 *     P0[3:0] = red columns 0-3, P0[7:4] = blue columns 0-3.
 *   P2[7:0] = layer anode select (active-low via PNP).
 *     Scan table: 0xFE,0xFD,0xFB,0xF7,0xEF,0xDF,0xBF,0x7F
 *     (8 scan lines: 4 layers x 2 color groups)
 *
 * Build:
 *   sdcc -mmcs51 --model-small -o build/20-ledcube.ihx src/20-ledcube/main.c
 */

#include <stc12.h>
#include <stdint.h>

/* ---- P0 data polarity (UNVERIFIED) ----
 *
 * The spec says active-low: a 0 bit lights the LED.
 * probe.c (the measurement instrument) assumes active-high: its
 * blank frame is {0,…} and it probes with (1 << bit).
 *
 * These two readings of the same hardware disagree.  Only the
 * physical probe on a real cube can settle it.  Until then, this
 * constant controls the assumption so flipping it is one line.
 *
 * P0_ACTIVE_LOW = 1:  0 = LED on,  0xFF = all off  (spec §2)
 * P0_ACTIVE_LOW = 0:  1 = LED on,  0x00 = all off  (probe.c)
 */
#define P0_ACTIVE_LOW  1

#if P0_ACTIVE_LOW
#define FB_ALL_OFF  0xFF
#define FB_ALL_ON   0x00
#else
#define FB_ALL_OFF  0x00
#define FB_ALL_ON   0xFF
#endif

/* ---- scan table ---- */
static const uint8_t __code scan_table[8] = {
    0xFE, 0xFD, 0xFB, 0xF7,   /* layers 0-3, first color pass  */
    0xEF, 0xDF, 0xBF, 0x7F    /* layers 0-3, second color pass */
};

/* ---- framebuffer: one byte per scan line ---- */
static uint8_t fb[8];

/* ---- timing ---- */

/*
 * Timer 0 polled delay at FOSC/12.
 *
 * A 12T and a 1T part count Timer 0 at FOSC/12 identically, so
 * the dwell is correct on both — unlike a software loop, which
 * runs ~12x too fast on the 1T STC12.
 *
 * At FOSC = 11,059,200 Hz the timer ticks at 921,600 Hz.
 * 1 ms = 921.6 ticks.  Mode 1 counts up from a reload value to
 * 0x10000, so reload = 65536 - 922 = 64614 = 0xFC66.
 * (Spec uses 0xFC67; the difference is < 1 µs per ms.)
 */
static void delay_ms(uint16_t ms) {
    AUXR &= ~0x80;                    /* T0 clocked at FOSC/12    */
    TMOD = (TMOD & 0xF0) | 0x01;     /* mode 1: 16-bit timer     */
    while (ms--) {
        TL0 = 0x66;                  /* reload for ~1 ms         */
        TH0 = 0xFC;
        TF0 = 0;                     /* clear overflow flag      */
        TR0 = 1;                     /* start                    */
        while (!TF0)                 /* poll until overflow      */
            ;
        TR0 = 0;                     /* stop                     */
        TF0 = 0;                     /* clear for next iteration */
    }
}

/* ---- scan engine ---- */

/*
 * Scan through all 8 lines once.
 * Calculated: 8 × 1 ms dwell ≈ 8 ms per scan, ~125 Hz refresh.
 * These figures are from the reload arithmetic, not measured
 * under the emulator — treat as estimates until verified.
 */
static void scan_once(void) {
    uint8_t line;
    for (line = 0; line < 8; line++) {
        P0 = fb[line];              /* set column data           */
        P2 = scan_table[line];      /* activate one layer        */
        delay_ms(1);                /* dwell                     */
        P2 = 0xFF;                  /* all layers off (no ghost) */
    }
}

/*
 * Hold the current framebuffer on-screen for approximately
 * `duration_ms` milliseconds by repeated scanning.
 */
static void hold(uint16_t duration_ms) {
    uint16_t elapsed = 0;
    while (elapsed < duration_ms) {
        scan_once();    /* ~8 ms per full scan (calculated) */
        elapsed += 8;
    }
}

/* ---- framebuffer helpers ---- */

static void fb_clear(void) {
    uint8_t i;
    for (i = 0; i < 8; i++) fb[i] = FB_ALL_OFF;
}

/*
 * Set red column mask for a layer (0-3).
 * mask: bit0=col0 .. bit3=col3, 1 = "light this LED".
 * The helpers translate to the hardware polarity via P0_ACTIVE_LOW.
 */
static void fb_set_red(uint8_t layer, uint8_t mask) {
#if P0_ACTIVE_LOW
    fb[layer] = (fb[layer] | 0x0F) & ~(mask & 0x0F);
#else
    fb[layer] = (fb[layer] & 0xF0) | (mask & 0x0F);
#endif
}

/* Set blue column mask for a layer (0-3). */
static void fb_set_blue(uint8_t layer, uint8_t mask) {
#if P0_ACTIVE_LOW
    fb[layer + 4] = (fb[layer + 4] | 0xF0) & ~((mask & 0x0F) << 4);
#else
    fb[layer + 4] = (fb[layer + 4] & 0x0F) | ((mask & 0x0F) << 4);
#endif
}

/* ---- animation patterns ---- */

/*
 * Pattern 1: All on -- every LED lit for ~3 seconds.
 */
static void pattern_all_on(void) {
    uint8_t i;
    for (i = 0; i < 8; i++) fb[i] = FB_ALL_ON;
    hold(3000);
}

/*
 * Pattern 2: Layer sweep (bounce) -- red LEDs only.
 * Layers 0,1,2,3,2,1,0,1,2,3,...  ~200 ms each, ~3 s total.
 */
static void pattern_layer_sweep(void) {
    static const uint8_t __code bounce[] = {0,1,2,3,2,1};
    uint8_t step;
    uint8_t reps;
    for (reps = 0; reps < 3; reps++) {        /* 3 full bounces ~ 3.6 s */
        for (step = 0; step < 6; step++) {
            uint8_t layer = bounce[step];
            fb_clear();
            fb_set_red(layer, 0x0F);          /* all 4 red columns */
            hold(200);
        }
    }
}

/*
 * Pattern 3: Rain -- blue LEDs fall from top to bottom.
 * 4 drops at staggered column positions, cycling through rotations.
 * ~100 ms per step, ~3 s total.
 */
static void pattern_rain(void) {
    /*
     * 4 rotation sets -- each lights one column per layer,
     * staggered so the drops appear to fall.
     */
    static const uint8_t __code col_rot[4][4] = {
        {0, 1, 2, 3},
        {1, 2, 3, 0},
        {2, 3, 0, 1},
        {3, 0, 1, 2}
    };
    uint8_t rot, frame;

    for (rot = 0; rot < 4; rot++) {
        /* 8 frames per rotation: drop enters at top, exits at bottom */
        for (frame = 0; frame < 8; frame++) {
            uint8_t layer;
            fb_clear();
            for (layer = 0; layer < 4; layer++) {
                /*
                 * Each column's drop is offset by `layer` frames.
                 * drop_pos = frame - layer  =>  visible when 0..3
                 */
                int8_t drop_pos = (int8_t)frame - (int8_t)layer;
                if (drop_pos >= 0 && drop_pos < 4) {
                    uint8_t vis_layer = 3 - (uint8_t)drop_pos;  /* top=3 down to 0 */
                    uint8_t col = col_rot[rot][layer];
                    fb_set_blue(vis_layer, (uint8_t)(1 << col));
                }
            }
            hold(100);
        }
    }
}

/*
 * Pattern 4: Spiral -- one column lit at a time, sweeping through
 * all 4 columns on each layer, bottom to top.  Both colors.
 * ~100 ms per position, ~3.2 s total.
 */
static void pattern_spiral(void) {
    /* Spiral order through the 4 columns */
    static const uint8_t __code spiral_cols[] = {0, 1, 3, 2};
    uint8_t pass, layer, ci;

    for (pass = 0; pass < 2; pass++) {
        for (layer = 0; layer < 4; layer++) {
            for (ci = 0; ci < 4; ci++) {
                uint8_t col = spiral_cols[ci];
                uint8_t bit = (uint8_t)(1 << col);
                fb_clear();
                fb_set_red(layer, bit);
                fb_set_blue(layer, bit);
                hold(100);
            }
        }
    }
}

/* ---- main ---- */

void main(void) {
    P0 = FB_ALL_OFF;  /* all LEDs off  */
    P2 = 0xFF;        /* all layers off (always active-low) */

    while (1) {
        pattern_all_on();
        pattern_layer_sweep();
        pattern_rain();
        pattern_spiral();
    }
}
