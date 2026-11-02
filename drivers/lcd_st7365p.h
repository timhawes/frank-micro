/*
 * frank-micro — BBC Micro for RP2350
 *
 * Copyright (c) 2026 Tim Hawes <me@timhawes.com>
 * https://github.com/rh1tech/frank-micro
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * lcd_st7365p.h — PicoCalc LCD scanout, the parts that are not the shared
 * graphics_* interface in HDMI.h.  Built only as HDMI_DRIVER=LCD_SPI.
 */
#ifndef LCD_ST7365P_H
#define LCD_ST7365P_H

#include <stdint.h>

/* Incremented by every graphics_set_buffer(), which frank_gui.c calls once
 * per emulated frame to present the finished buffer.  The status line reports
 * its rate. */
extern volatile uint32_t lcd_present_count;

/* Incremented by core 1 each time it finishes sending a frame to the panel.
 * Lower than lcd_present_count when the SPI link cannot keep up; core 1 then
 * skips to the newest frame. */
extern volatile uint32_t lcd_frames_sent;

/* Incremented by core 1 for each row it sends.  Only rows that changed since
 * the panel last received them are sent, so this measures the real work. */
extern volatile uint32_t lcd_rows_sent;

/* Microseconds core 1 has spent waiting for the panel's scan to be clear of
 * the rows it is about to write (lcd_wait_scan()). */
extern volatile uint32_t lcd_wait_us;

/* Core 0, at the start of each emulated frame: which of the two framebuffers
 * to draw into.  Never the one core 1 is sending, and, while core 1 is idle,
 * not the newest complete frame it has yet to take.  Never waits beyond a
 * few instructions under a spin lock. */
uint8_t *lcd_begin_frame(uint8_t *a, uint8_t *b);

/* The status line: LCD_STATUS_H rows of 320 palette indices, shown at panel
 * row LCD_STATUS_Y, in the band below the BBC picture.  Core 0 calls this
 * whenever the line changes; it copies the rows and returns. */
#define LCD_STATUS_H 10
#define LCD_STATUS_Y 299
void lcd_status_set(const uint8_t *pix);

#endif /* LCD_ST7365P_H */
