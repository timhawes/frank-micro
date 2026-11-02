/*
 * frank-micro — BBC Micro for RP2350
 *
 * Copyright (c) 2026 Tim Hawes <me@timhawes.com>
 * https://github.com/rh1tech/frank-micro
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * frank_kbd_picocalc.h — PicoCalc keyboard to the BBC key matrix.
 * Built only for PLATFORM_PICOCALC.
 */
#ifndef FRANK_KBD_PICOCALC_H
#define FRANK_KBD_PICOCALC_H

void frank_kbd_picocalc_init(void);   /* once, from main() */
void frank_kbd_picocalc_poll(void);   /* once a frame, from frank_keyboard_poll() */

/* The joystick setting: when it is not Off, the arrows are the d-pad, Alt is
 * fire 1 and Ctrl fire 2.  ORs them into now[FRANK_PAD_ACTION_COUNT]. */
void frank_kbd_picocalc_pad(int now[]);

#endif /* FRANK_KBD_PICOCALC_H */
