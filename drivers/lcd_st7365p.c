/*
 * frank-micro — BBC Micro for RP2350
 *
 * Copyright (c) 2026 Tim Hawes <me@timhawes.com>
 * https://github.com/rh1tech/frank-micro
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * lcd_st7365p.c — video driver for the PicoCalc's 320x320 ST7365P SPI LCD.
 *
 * Phase 1 of the PicoCalc port: a stub.  It implements the graphics_*
 * interface from HDMI.h so the application links and runs unchanged, keeps
 * the palette so screenshots still work, and counts presents for the status
 * line, but does no panel I/O.  The panel stays as the bootloader left it.
 *
 * Phase 2 replaces the internals with a core-1 scanout.  See
 * docs/PICOCALC.md section 5.1.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "HDMI.h"
#include "lcd_st7365p.h"

/* Declared in HDMI.h for the VGA autodetect.  There is no VGA here, and
 * main() never sets it on this platform, but the symbol has to exist. */
bool SELECT_VGA = false;

volatile uint32_t lcd_present_count = 0;

static uint8_t  *s_buffer = NULL;
static int       s_width  = 320;
static int       s_height = 256;
static uint32_t  s_palette[256];
static bool      s_crt_active = false;
static bool      s_greyscale  = false;

void graphics_init(g_out out) {
    (void)out;
    printf("LCD: stub (phase 1), no panel output\n");
}

void graphics_set_buffer(uint8_t *buffer) {
    s_buffer = buffer;
    lcd_present_count++;
}

uint8_t *graphics_get_buffer(void)  { return s_buffer; }
uint32_t graphics_get_width(void)   { return (uint32_t)s_width; }
uint32_t graphics_get_height(void)  { return (uint32_t)s_height; }

void graphics_set_res(int w, int h) { s_width = w; s_height = h; }
void graphics_set_shift(int x, int y) { (void)x; (void)y; }
void graphics_set_mode(enum graphics_mode_t mode) { (void)mode; }

void graphics_set_palette(uint8_t i, uint32_t color888) { s_palette[i] = color888 & 0xFFFFFFu; }
uint32_t graphics_get_palette(uint8_t i) { return s_palette[i]; }
void graphics_set_bgcolor(uint32_t color888) { (void)color888; }
void graphics_restore_sync_colors(void) {}

void graphics_set_crt_active(bool active) { s_crt_active = active; }
bool graphics_get_crt_active(void)        { return s_crt_active; }
void graphics_set_greyscale(bool active)  { s_greyscale = active; }
bool graphics_get_greyscale(void)         { return s_greyscale; }
