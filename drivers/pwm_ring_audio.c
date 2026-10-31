/*
 * frank-micro — BBC Micro for RP2350
 *
 * Copyright (c) 2026 Tim Hawes <me@timhawes.com>
 * https://github.com/rh1tech/frank-micro
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * pwm_ring_audio.c — PWM audio from a ring buffer, for the PicoCalc.
 *
 * One PWM slice drives both speaker pins and paces one DMA channel: each
 * wrap of the slice, once per sample, requests one 32-bit write into the
 * slice's packed CC register.  The channel reads a 16 KB ring once through,
 * then chains to a second channel that points it back at the start, so the
 * pair loops for ever with no interrupt.  Where it has got to is the first
 * channel's own READ_ADDR register.
 *
 * Not the DMA's own read-address wrapping: that needs the ring aligned to
 * its size, and a 16 KB-aligned array pushes .bss up by about 10 KB of
 * padding.
 *
 * The producer writes ahead of that position and never waits.  It replaces
 * pwm_audio_push_samples_blocking(), which spun until a buffer was free and
 * so let the audio clock pace the emulator (docs/HANDOFF.md).
 *
 * The emulator produces samples a little faster than 31,250 Hz, because each
 * frame overshoots its 40,000-cycle limit and the overshoot is never paid
 * back.  A ring alone would fill up and then drop a 28 ms block every few
 * minutes.  So the push steers the amount queued towards RING_TARGET by
 * skipping or repeating at most one sample per block, which is inaudible.
 */
#include "pwm_ring_audio.h"

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

#include <stdio.h>

#define RING_BITS     12
#define RING_LEN      (1u << RING_BITS)        /* 4096 samples, 131 ms */
#define RING_MASK     (RING_LEN - 1u)
#define RING_TARGET   2048u                     /* queued just after a push */
#define RING_BAND     256u                      /* servo dead band          */
#define RING_HEADROOM 512u                      /* never fill beyond LEN-this */
#define RING_GUARD    64u                       /* below this, call it an underrun */
#define AVG_SHIFT     4                         /* fill average: 1/16 per push */

/* Channels 8 and 9 are the PWM path's on every board; nothing else on the
 * PicoCalc uses them, and the LCD claims its channels dynamically from 0 up. */
#define RING_DMA_CH   8
#define LOOP_DMA_CH   9

static uint32_t ring[RING_LEN];
static uint32_t *s_ring_start = ring;   /* what LOOP_DMA_CH copies back */

static uint32_t s_rate;
static int32_t  s_center, s_max, s_swing;
static uint32_t s_wr;                /* next index to write                 */
static uint32_t s_last_fill;         /* fill after the previous push        */
static uint32_t s_last_us;           /* time of the previous push           */
static uint32_t s_avg;               /* fill average, << AVG_SHIFT          */
static bool     s_started;
static pwm_ring_stats_t s_stats;

static inline uint32_t silence_word(void) {
    return ((uint32_t)s_center << 16) | (uint32_t)s_center;
}

static inline uint32_t dma_read_index(void) {
    /* At the moment between laps it reads one past the end, which masks to 0:
     * the right answer, as that is where it is about to read. */
    uint32_t a = dma_hw->ch[RING_DMA_CH].read_addr;
    return ((a - (uint32_t)(uintptr_t)ring) >> 2) & RING_MASK;
}

void pwm_ring_init(uint pin_l, uint pin_r, uint32_t sample_rate) {
    s_rate = sample_rate;

    uint32_t wrap = clock_get_hz(clk_sys) / sample_rate - 1u;
    if (wrap > 65535u) wrap = 65535u;
    s_center = (int32_t)(wrap / 2u);
    s_max    = (int32_t)wrap;
    s_swing  = (int32_t)(wrap >> 1);

    uint slice = pwm_gpio_to_slice_num(pin_l);
    if (pwm_gpio_to_slice_num(pin_r) != slice)
        printf("PWM ring: pins %u and %u are on different slices; right is silent\n",
               pin_l, pin_r);
    gpio_set_function(pin_l, GPIO_FUNC_PWM);
    gpio_set_function(pin_r, GPIO_FUNC_PWM);

    pwm_config pc = pwm_get_default_config();
    pwm_config_set_clkdiv(&pc, 1.0f);
    pwm_config_set_wrap(&pc, (uint16_t)wrap);
    pwm_init(slice, &pc, false);
    pwm_set_both_levels(slice, (uint16_t)s_center, (uint16_t)s_center);

    for (uint32_t i = 0; i < RING_LEN; i++) ring[i] = silence_word();

    dma_channel_claim(RING_DMA_CH);
    dma_channel_claim(LOOP_DMA_CH);

    /* The loop channel: one word, &ring[0], into the data channel's read
     * address trigger alias.  That restarts it with its transfer count
     * reloaded to RING_LEN. */
    dma_channel_config lc = dma_channel_get_default_config(LOOP_DMA_CH);
    channel_config_set_transfer_data_size(&lc, DMA_SIZE_32);
    channel_config_set_read_increment(&lc, false);
    channel_config_set_write_increment(&lc, false);
    dma_channel_configure(LOOP_DMA_CH, &lc, &dma_hw->ch[RING_DMA_CH].al3_read_addr_trig,
                          &s_ring_start, 1, false);

    dma_channel_config dc = dma_channel_get_default_config(RING_DMA_CH);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, true);
    channel_config_set_write_increment(&dc, false);
    channel_config_set_dreq(&dc, DREQ_PWM_WRAP0 + slice);
    channel_config_set_chain_to(&dc, LOOP_DMA_CH);
    dma_channel_configure(RING_DMA_CH, &dc, &pwm_hw->slice[slice].cc, ring,
                          RING_LEN, true);
    pwm_set_enabled(slice, true);

    printf("PWM ring: slice=%u rate=%lu wrap=%lu ring=%u target=%u\n",
           slice, (unsigned long)sample_rate, (unsigned long)wrap,
           (unsigned)RING_LEN, (unsigned)RING_TARGET);
}

static inline uint32_t level(int16_t s) {
    int32_t v = (((int32_t)s * s_swing) >> 15) + s_center;
    if (v < 0) v = 0;
    if (v > s_max) v = s_max;
    return ((uint32_t)v << 16) | (uint32_t)v;
}

void pwm_ring_push(const int16_t *s, int n) {
    if (n <= 0 || !s_rate) return;
    s_stats.pushes++;

    uint32_t rd  = dma_read_index();
    uint32_t now = time_us_32();

    /* How much should be left, from the clock alone?  The fill measured from
     * the read pointer is modulo the ring, so once the output has overtaken
     * the producer it cannot tell an empty ring from a full one.  The time
     * since the last push can: both rates come from the same crystal. */
    bool underrun = !s_started;
    if (s_started) {
        uint64_t played = ((uint64_t)(now - s_last_us) * s_rate) / 1000000u;
        if (played + RING_GUARD >= s_last_fill) underrun = true;
    }

    uint32_t fill;
    if (underrun) {
        /* Restart RING_TARGET - n ahead of the output, with silence in the
         * gap so the stale samples the DMA is about to read are not replayed. */
        if (s_started) s_stats.underruns++;
        s_started = true;
        uint32_t gap = RING_TARGET - (uint32_t)n;
        for (uint32_t i = 8; i < gap; i++) ring[(rd + i) & RING_MASK] = silence_word();
        s_wr  = (rd + gap) & RING_MASK;
        fill  = gap;
        s_avg = RING_TARGET << AVG_SHIFT;
    } else {
        fill = (s_wr - rd) & RING_MASK;
    }

    if (fill + (uint32_t)n + 1u > RING_LEN - RING_HEADROOM) {
        /* No room.  Only when the producer is far ahead, as with Limit Speed
         * off.  Drop the whole block rather than tear into the queued audio. */
        s_stats.dropped++;
        s_last_fill = fill;
        s_last_us   = now;
        s_stats.fill = fill;
        return;
    }

    /* Steer the average fill towards RING_TARGET, one sample per block at
     * most.  The average smooths out where in the 20 ms frame the push
     * happened to land. */
    int count = n;
    int extra = 0;
    uint32_t avg = s_avg >> AVG_SHIFT;
    if (avg > RING_TARGET + RING_BAND)      { count = n - 1; s_stats.trim++; }
    else if (avg + RING_BAND < RING_TARGET) { extra = 1;     s_stats.trim--; }

    uint32_t w = s_wr;
    uint32_t v = 0;
    for (int i = 0; i < count; i++) {
        v = level(s[i]);
        ring[w] = v;
        w = (w + 1u) & RING_MASK;
    }
    if (extra) {
        ring[w] = count ? v : silence_word();
        w = (w + 1u) & RING_MASK;
    }
    s_wr = w;

    fill += (uint32_t)(count + extra);
    s_avg += fill - (s_avg >> AVG_SHIFT);
    s_last_fill  = fill;
    s_last_us    = now;
    s_stats.fill = fill;
}

void pwm_ring_get_stats(pwm_ring_stats_t *out) {
    *out = s_stats;
}
