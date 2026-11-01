/*
 * frank-micro — BBC Micro for RP2350
 *
 * Copyright (c) 2026 Tim Hawes <me@timhawes.com>
 * https://github.com/rh1tech/frank-micro
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PWM_RING_AUDIO_H
#define PWM_RING_AUDIO_H

#include <stdint.h>
#include "pico/types.h"

/* Start PWM output on one slice (both pins must share it) at sample_rate,
 * fed by a DMA channel that plays a ring buffer for ever. */
void pwm_ring_init(uint pin_l, uint pin_r, uint32_t sample_rate);

/* Queue n mono samples.  Never waits.  Absorbs a small difference between
 * the producer's rate and the output's by repeating or skipping at most one
 * sample per call. */
void pwm_ring_push(const int16_t *s, int n);

/* Silence what has already been played.  Call about once a frame, from the
 * same core as pwm_ring_push(), whether or not anything is being pushed:
 * without it a producer that stops leaves the ring looping. */
void pwm_ring_service(void);

typedef struct {
    uint32_t pushes;     /* calls to pwm_ring_push()                        */
    uint32_t dropped;    /* whole blocks dropped: no room (producer too fast) */
    uint32_t underruns;  /* output caught up with the producer; resynced    */
    int32_t  trim;       /* samples skipped (+) or repeated (-), cumulative */
    uint32_t fill;       /* samples queued after the last push             */
} pwm_ring_stats_t;

void pwm_ring_get_stats(pwm_ring_stats_t *out);

#endif
