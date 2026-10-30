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
 * Phase 2 of the PicoCalc port: the simple scanout described at the end of
 * docs/PICOCALC.md section 5.1.  Core 1 owns the panel.  It initialises it,
 * then sends every row of every presented frame with blocking SPI writes at
 * 24 MHz.  Changed-row tracking and DMA are Phase 5.
 *
 * Core 0 never waits on the panel.  graphics_set_buffer() is a pointer store
 * and a counter increment; core 1 picks the newest frame up when it finishes
 * the one it is sending, so frames it cannot keep up with are skipped.  The
 * panel keeps its own frame memory, so a skipped frame costs nothing visible
 * but motion smoothness.
 *
 * The 320x256 BBC frame is centred on the 320x320 panel: 32 rows above and
 * below, painted black once at init and otherwise left alone.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "pico.h"
#include "pico/multicore.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"

#include "board_config.h"
#include "HDMI.h"
#include "lcd_st7365p.h"

/* Declared in HDMI.h for the VGA autodetect.  There is no VGA here, and
 * main() never sets it on this platform, but the symbol has to exist. */
bool SELECT_VGA = false;

#define LCD_W        320
#define LCD_H        320
#define LCD_SPI_HZ   (24u * 1000u * 1000u)  /* clk_peri 48 MHz / 2; Phase 5 raises it */

/* Set to 1 if red and blue come out swapped on the panel.  MADCTL sets the
 * BGR bit, as ClockworkPi's sample does, which should make plain RGB565 come
 * out right. */
#define LCD_SWAP_RB  0

volatile uint32_t lcd_present_count = 0;
volatile uint32_t lcd_frames_sent   = 0;

static uint8_t * volatile s_buffer = NULL;
static int       s_width  = 320;
static int       s_height = 256;
static uint32_t  s_palette[256];
static uint16_t  s_rgb565[256];       /* already byte-swapped for the wire */
static bool      s_crt_active = false;
static bool      s_greyscale  = false;

/* ── SPI and pin helpers, core 1 only ─────────────────────────────────────── */

static inline void lcd_cs(bool level) { gpio_put(LCD_PIN_CS, level); }
static inline void lcd_dc(bool level) { gpio_put(LCD_PIN_DC, level); }

static void lcd_cmd(uint8_t cmd) {
    lcd_dc(0);
    lcd_cs(0);
    spi_write_blocking(LCD_SPI_PORT, &cmd, 1);
    lcd_cs(1);
}

static void lcd_cmd_data(uint8_t cmd, const uint8_t *data, size_t len) {
    lcd_dc(0);
    lcd_cs(0);
    spi_write_blocking(LCD_SPI_PORT, &cmd, 1);
    if (len) {
        lcd_dc(1);
        spi_write_blocking(LCD_SPI_PORT, data, len);
    }
    lcd_cs(1);
}

/* Set the write window and leave CS low with DC high, ready for pixel data.
 * The caller raises CS when the window is full. */
static void lcd_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    uint8_t caset[4] = { x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF };
    uint8_t raset[4] = { y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF };
    lcd_cmd_data(0x2A, caset, 4);     /* CASET */
    lcd_cmd_data(0x2B, raset, 4);     /* RASET */
    uint8_t ramwr = 0x2C;             /* RAMWR */
    lcd_dc(0);
    lcd_cs(0);
    spi_write_blocking(LCD_SPI_PORT, &ramwr, 1);
    lcd_dc(1);
}

/*
 * The init sequence is ClockworkPi's, from pico_lcd_init() in
 * Code/picocalc_helloworld/lcdspi/lcdspi.c at commit fdcfe2c, byte for byte,
 * with one deliberate change: COLMOD 0x55 (16-bit RGB565) where the sample
 * uses 0x66 (18-bit), so a pixel is two bytes on the wire rather than three.
 * See docs/PICOCALC.md section 5.1.
 */
static void lcd_panel_init(void) {
    gpio_put(LCD_PIN_RST, 1); sleep_ms(10);
    gpio_put(LCD_PIN_RST, 0); sleep_ms(10);
    gpio_put(LCD_PIN_RST, 1); sleep_ms(200);

    static const uint8_t gamma_p[] = { 0x00, 0x03, 0x09, 0x08, 0x16, 0x0A, 0x3F, 0x78,
                                       0x4C, 0x09, 0x0A, 0x08, 0x16, 0x1A, 0x0F };
    static const uint8_t gamma_n[] = { 0x00, 0x16, 0x19, 0x03, 0x0F, 0x05, 0x32, 0x45,
                                       0x46, 0x04, 0x0E, 0x0D, 0x35, 0x37, 0x0F };
    lcd_cmd_data(0xE0, gamma_p, sizeof gamma_p);                 /* positive gamma */
    lcd_cmd_data(0xE1, gamma_n, sizeof gamma_n);                 /* negative gamma */
    lcd_cmd_data(0xC0, (const uint8_t[]){ 0x17, 0x15 }, 2);      /* power control 1 */
    lcd_cmd_data(0xC1, (const uint8_t[]){ 0x41 }, 1);            /* power control 2 */
    lcd_cmd_data(0xC5, (const uint8_t[]){ 0x00, 0x12, 0x80 }, 3);/* VCOM */
    lcd_cmd_data(0x36, (const uint8_t[]){ 0x48 }, 1);            /* MADCTL: MX | BGR */
    lcd_cmd_data(0x3A, (const uint8_t[]){ 0x55 }, 1);            /* COLMOD: 16-bit */
    lcd_cmd_data(0xB0, (const uint8_t[]){ 0x00 }, 1);            /* interface mode */
    lcd_cmd_data(0xB1, (const uint8_t[]){ 0xA0 }, 1);            /* frame rate */
    lcd_cmd(0x21);                                               /* INVON */
    lcd_cmd_data(0xB4, (const uint8_t[]){ 0x02 }, 1);            /* inversion control */
    lcd_cmd_data(0xB6, (const uint8_t[]){ 0x02, 0x02, 0x3B }, 3);/* display function */
    lcd_cmd_data(0xB7, (const uint8_t[]){ 0xC6 }, 1);            /* entry mode */
    lcd_cmd_data(0xE9, (const uint8_t[]){ 0x00 }, 1);
    lcd_cmd_data(0xF7, (const uint8_t[]){ 0xA9, 0x51, 0x2C, 0x82 }, 4); /* adjust control 3 */
    lcd_cmd(0x11);                                               /* SLPOUT */
    sleep_ms(120);
    lcd_cmd(0x29);                                               /* DISPON */
    sleep_ms(120);
}

/* One row of pixels on the wire: 320 RGB565 values, big-endian. */
static uint16_t s_line[LCD_W];

static void lcd_clear(void) {
    for (int x = 0; x < LCD_W; x++) s_line[x] = 0;
    lcd_window(0, 0, LCD_W - 1, LCD_H - 1);
    for (int y = 0; y < LCD_H; y++)
        spi_write_blocking(LCD_SPI_PORT, (const uint8_t *)s_line, sizeof s_line);
    lcd_cs(1);
}

/* Send one whole frame: 256 rows of 320 palette indices, centred. */
static void __not_in_flash_func(lcd_push_frame)(const uint8_t *fb) {
    const int y0 = (LCD_H - 256) / 2;
    lcd_window(0, y0, LCD_W - 1, y0 + 256 - 1);
    for (int y = 0; y < 256; y++) {
        const uint8_t *src = fb + y * 320;
        for (int x = 0; x < LCD_W; x++)
            s_line[x] = s_rgb565[src[x]];
        spi_write_blocking(LCD_SPI_PORT, (const uint8_t *)s_line, sizeof s_line);
    }
    lcd_cs(1);
}

static void __not_in_flash_func(lcd_core1_run)(void) {
    lcd_panel_init();
    lcd_clear();
    printf("LCD: panel initialised on core 1\n");

    uint32_t seen = lcd_present_count;
    for (;;) {
        uint32_t now = lcd_present_count;
        if (now == seen) {
            tight_loop_contents();
            continue;
        }
        seen = now;
        uint8_t *fb = s_buffer;
        if (fb) {
            lcd_push_frame(fb);
            lcd_frames_sent++;
        }
    }
}

/* ── graphics_* interface ─────────────────────────────────────────────────── */

void graphics_init(g_out out) {
    (void)out;
    spi_init(LCD_SPI_PORT, LCD_SPI_HZ);
    spi_set_format(LCD_SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(LCD_PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(LCD_PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(LCD_PIN_MISO, GPIO_FUNC_SPI);

    gpio_init(LCD_PIN_CS);  gpio_set_dir(LCD_PIN_CS,  GPIO_OUT); gpio_put(LCD_PIN_CS,  1);
    gpio_init(LCD_PIN_DC);  gpio_set_dir(LCD_PIN_DC,  GPIO_OUT); gpio_put(LCD_PIN_DC,  1);
    gpio_init(LCD_PIN_RST); gpio_set_dir(LCD_PIN_RST, GPIO_OUT); gpio_put(LCD_PIN_RST, 1);

    /* The panel's reset and init take about 460 ms of sleeps.  They run on
     * core 1 so core 0 carries straight on with the boot. */
    multicore_launch_core1(lcd_core1_run);
    printf("LCD: SPI1 at %u Hz, panel init on core 1\n",
           (unsigned)spi_get_baudrate(LCD_SPI_PORT));
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

void graphics_set_palette(uint8_t i, uint32_t color888) {
    s_palette[i] = color888 & 0xFFFFFFu;
    uint32_t r = (color888 >> 16) & 0xFF, g = (color888 >> 8) & 0xFF, b = color888 & 0xFF;
#if LCD_SWAP_RB
    uint32_t t = r; r = b; b = t;
#endif
    uint16_t c = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    s_rgb565[i] = (uint16_t)((c >> 8) | (c << 8));   /* high byte first on the wire */
}
uint32_t graphics_get_palette(uint8_t i) { return s_palette[i]; }
void graphics_set_bgcolor(uint32_t color888) { (void)color888; }
void graphics_restore_sync_colors(void) {}

void graphics_set_crt_active(bool active) { s_crt_active = active; }
bool graphics_get_crt_active(void)        { return s_crt_active; }
void graphics_set_greyscale(bool active)  { s_greyscale = active; }
bool graphics_get_greyscale(void)         { return s_greyscale; }
