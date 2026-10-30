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

#endif /* LCD_ST7365P_H */
