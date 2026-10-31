/*
 * frank-micro — BBC Micro for RP2350
 *
 * Copyright (c) 2026 Tim Hawes <me@timhawes.com>
 * https://github.com/rh1tech/frank-micro
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * picocalc_kbd.h — the PicoCalc keyboard controller's event FIFO, read over
 * I2C without ever blocking.  See docs/PICOCALC.md section 5.2.
 */
#ifndef PICOCALC_KBD_H
#define PICOCALC_KBD_H

#include <stdint.h>

/* Event states, as the keyboard firmware reports them. */
enum {
    PICOCALC_KBD_PRESSED  = 1,
    PICOCALC_KBD_HOLD     = 2,
    PICOCALC_KBD_RELEASED = 3,
};

typedef void (*picocalc_kbd_event_fn)(uint8_t state, uint8_t code);

/* Set up i2c1.  Events already queued in the controller are discarded. */
void picocalc_kbd_init(void);

/* Call once a frame.  Hands every event that arrived since the last call to
 * fn, in order, then queues the next reads and returns at once. */
void picocalc_kbd_poll(picocalc_kbd_event_fn fn);

/* Bus errors and stalled reads since boot.  Reported on the STAT line. */
uint32_t picocalc_kbd_errors(void);

#endif /* PICOCALC_KBD_H */
