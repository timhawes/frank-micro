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
 * The scanout described in docs/PICOCALC.md section 5.1.  Core 1 owns the
 * panel.  It initialises it, then sends each complete frame's changed rows by
 * DMA at 63 MHz, timing each send against the panel's own refresh so the
 * scan does not cross it (lcd_wait_scan()).
 *
 * Core 0 never waits on the panel.  Core 1 sends only complete frames, and
 * core 0 never draws into the one being sent: see lcd_begin_frame().  Frames
 * core 1 cannot keep up with are skipped whole.  The panel keeps its own
 * frame memory, so a skipped frame costs nothing visible but motion
 * smoothness.
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
#include "hardware/sync.h"
#include "hardware/dma.h"

#include "board_config.h"
#include "HDMI.h"
#include "lcd_st7365p.h"

/* Declared in HDMI.h for the VGA autodetect.  There is no VGA here, and
 * main() never sets it on this platform, but the symbol has to exist. */
bool SELECT_VGA = false;

#define LCD_W        320
#define LCD_H        320
/* clk_peri is clk_sys, 252 MHz (frank_platform.c), so SPI runs at 252 / 2n:
 * 25.2, 31.5, 42 or 63 MHz.  The panel is rated to 15 MHz; how far above
 * that it stays clean was found on the board (docs/HANDOFF.md). */
#define LCD_SPI_HZ   (63000u * 1000u)

/* Set to 1 if red and blue come out swapped on the panel.  MADCTL sets the
 * BGR bit, as ClockworkPi's sample does, which should make plain RGB565 come
 * out right. */
#define LCD_SWAP_RB  0

volatile uint32_t lcd_present_count = 0;
volatile uint32_t lcd_frames_sent   = 0;

static uint8_t * volatile s_buffer = NULL;

/*
 * Which buffer is where.  Both under s_lock, which each side holds for a few
 * instructions, so core 0 never waits on core 1 for longer than that.
 *
 *   s_latest   the newest complete frame core 1 has not taken, or NULL
 *   s_sending  the frame core 1 is sending, or NULL
 *
 * Until Phase 5 core 0 flipped buffers every frame regardless, and a send
 * took nearly three emulated frames, so the panel got rows from up to three
 * different BBC frames in one picture.  It showed as tearing on anything
 * moving across the whole screen.
 */
static spin_lock_t *s_lock;
static uint8_t *s_latest;
static uint8_t *s_sending;
static int       s_width  = 320;
static int       s_height = 256;
static uint32_t  s_palette[256];
static uint16_t  s_rgb565[256];       /* already byte-swapped for the wire */
static bool      s_crt_active = false;
static bool      s_greyscale  = false;

/* ── SPI and pin helpers, core 1 only ─────────────────────────────────────── */

/*
 * Every change of CS or DC is padded by a short wait on each side, about
 * 250 ns at 252 MHz.  Without it, at 24 MHz the panel intermittently missed
 * CASET, RASET or RAMWR: whole frames, and the power-on clear, went nowhere,
 * and the screen showed the panel's random power-on memory with an
 * occasional band of picture.  Every SPI timing on the panel was then far
 * tighter than the datasheet asks (its write clock is rated to 15 MHz), and
 * the same command sequence sent slowly from a debugger always worked.  The
 * cost is a few microseconds a frame.
 */
#define LCD_SETTLE_CYCLES 64
static inline void lcd_settle(void) { busy_wait_at_least_cycles(LCD_SETTLE_CYCLES); }
static inline void lcd_cs(bool level) { lcd_settle(); gpio_put(LCD_PIN_CS, level); lcd_settle(); }
static inline void lcd_dc(bool level) { lcd_settle(); gpio_put(LCD_PIN_DC, level); lcd_settle(); }

/*
 * Send bytes and return only once the last one has left the shifter.
 *
 * Anything followed by a change of DC or CS has to go through here, not
 * spi_write_blocking().  That function checks the busy flag immediately
 * after its last FIFO write, and from RAM at 252 MHz it can look before the
 * flag has risen, so it returns while the byte is still being shifted out.
 * The panel samples DC on the last bit of each byte, so raising DC early
 * turned RAMWR into a data byte: the panel never entered pixel-write mode
 * and every pixel was dropped.  Register writes survived only because they
 * ran from flash, which is slower.  Waiting for the bytes to be clocked back
 * in cannot return early.
 */
static void lcd_send_sync(const uint8_t *src, size_t len) {
    static uint8_t sink[LCD_W * 2];
    while (len) {
        size_t n = len < sizeof sink ? len : sizeof sink;
        spi_write_read_blocking(LCD_SPI_PORT, src, sink, n);
        src += n;
        len -= n;
    }
}

static void lcd_cmd(uint8_t cmd) {
    lcd_dc(0);
    lcd_cs(0);
    lcd_send_sync(&cmd, 1);
    lcd_cs(1);
}

static void lcd_cmd_data(uint8_t cmd, const uint8_t *data, size_t len) {
    lcd_dc(0);
    lcd_cs(0);
    lcd_send_sync(&cmd, 1);
    if (len) {
        lcd_dc(1);
        lcd_send_sync(data, len);
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
    lcd_send_sync(&ramwr, 1);
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
    for (int y = 0; y < LCD_H - 1; y++)
        spi_write_blocking(LCD_SPI_PORT, (const uint8_t *)s_line, sizeof s_line);
    lcd_send_sync((const uint8_t *)s_line, sizeof s_line);   /* last row: before CS rises */
    lcd_cs(1);
}

/*
 * DMA to the panel, one row at a time.  The TX channel feeds SPI1 from a line
 * buffer; the RX channel drains what comes back into a sink.  A row is done
 * when the RX channel is: by then every byte has been clocked out, so CS and
 * DC can change.  Waiting on TX, or on the SPI busy flag, can return while
 * the last byte is still in the shifter; see lcd_send_sync().
 */
static int s_dma_tx = -1, s_dma_rx = -1;
static uint8_t s_dma_sink;
static uint16_t __attribute__((aligned(4))) s_rows[2][LCD_W];   /* converted rows */

static void lcd_dma_init(void) {
    s_dma_tx = dma_claim_unused_channel(true);
    s_dma_rx = dma_claim_unused_channel(true);

    dma_channel_config c = dma_channel_get_default_config(s_dma_tx);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, spi_get_dreq(LCD_SPI_PORT, true));
    dma_channel_configure(s_dma_tx, &c, &spi_get_hw(LCD_SPI_PORT)->dr, NULL, 0, false);

    c = dma_channel_get_default_config(s_dma_rx);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, spi_get_dreq(LCD_SPI_PORT, false));
    dma_channel_configure(s_dma_rx, &c, &s_dma_sink, &spi_get_hw(LCD_SPI_PORT)->dr, 0, false);
}

static inline void lcd_dma_wait(void) {
    while (dma_channel_is_busy(s_dma_rx)) tight_loop_contents();
}

static inline void lcd_dma_start(const void *src, uint32_t len) {
    dma_channel_set_trans_count(s_dma_rx, len, true);    /* RX first: nothing overflows */
    dma_channel_transfer_from_buffer_now(s_dma_tx, src, len);
}

/*
 * Changed-row tracking.  row_sum[y] is a hash of the 320 palette indices of
 * row y as last sent; a row whose hash matches is not sent again.  A palette
 * change alters what the same indices look like, so it forces one whole
 * frame (s_force), as does the first.  A hash collision would leave one row
 * stale until it next changes; at 32 bits over 320 bytes that is negligible.
 */
static uint32_t s_row_sum[256];
static volatile bool s_force = true;
volatile uint32_t lcd_rows_sent = 0;

static inline uint32_t __not_in_flash_func(row_hash)(const uint8_t *row) {
    const uint32_t *w = (const uint32_t *)row;
    uint32_t h = 2166136261u;
    for (int i = 0; i < LCD_W / 4; i++) h = (h ^ w[i]) * 16777619u;
    return h;
}

static inline void __not_in_flash_func(row_convert)(uint16_t *dst, const uint8_t *src) {
    for (int x = 0; x < LCD_W; x++) dst[x] = s_rgb565[src[x]];
}

/*
 * Timing writes against the panel's own refresh.
 *
 * The panel redraws itself from its frame memory about 59 times a second,
 * top to bottom, whatever we do; TE, which would say when, is not wired.  If
 * that scan crosses the rows being written, one refresh shows the new frame
 * above the crossing and the old one below it: a tear line.  But the panel
 * can be read back over MISO, and RDSCANLINE (45h) says which line it is
 * scanning, so core 1 can choose when to start.
 *
 * The scan is faster than any write: about 28 lines a millisecond, against
 * about 12 rows for a write at 63 MHz.  So whichever side of the write the
 * scan starts on, it gains on the write front at the difference of the two
 * speeds.  It never crosses if the write is over before the scan has made up
 * the distance between them, measured forward from the first row written:
 *
 *     d = (scan - first) mod lines  <=  lines - margin - D * (v_scan - v_write)
 *
 * where D is how long the write will take.  The rows to send are known before
 * the send starts (lcd_push_frame() hashes them first), so D is too.  A small
 * update leaves almost the whole refresh to start in; a full frame about
 * 140 lines of 484, so it waits about 4 ms on average.  margin covers the
 * estimate of D and any offset between the scan count and the panel's rows.
 *
 * lcd_scan_calibrate() measures the scan once at boot.  If the panel does not
 * answer, s_scan_sync stays off and frames are sent as soon as they arrive.
 * s_scan_sync can also be cleared from a debugger, to compare.
 */
#define SCAN_READ_HZ    (8u * 1000u * 1000u)   /* reads are clean to 12 MHz; 16 MHz glitched */
#define SCAN_MARGIN     24                     /* lines */
#define RUN_OVERHEAD_US 10                     /* a new window: CASET, RASET, RAMWR */

volatile bool     s_scan_sync = false;
static uint32_t   s_scan_lines;                /* lines per refresh, porches included */
static uint32_t   s_scan_period_us;            /* one refresh                         */
static uint32_t   s_row_us;                    /* one row on the wire, with margin    */
volatile uint32_t lcd_wait_us = 0;             /* time spent waiting for the scan     */

/* Read the line the panel is scanning.  A read needs a slower clock than a
 * write, and its reply starts with one dummy bit, hence the shift. */
static uint32_t __not_in_flash_func(lcd_read_scanline)(void) {
    static const uint8_t zeros[3] = { 0, 0, 0 };
    uint8_t rx[3];
    uint8_t cmd = 0x45;                         /* RDSCANLINE */
    uint baud = spi_get_baudrate(LCD_SPI_PORT);
    spi_set_baudrate(LCD_SPI_PORT, SCAN_READ_HZ);
    lcd_dc(0);
    lcd_cs(0);
    lcd_send_sync(&cmd, 1);
    lcd_dc(1);
    spi_write_read_blocking(LCD_SPI_PORT, zeros, rx, 3);
    lcd_cs(1);
    spi_set_baudrate(LCD_SPI_PORT, baud);
    return ((((uint32_t)rx[0] << 16) | ((uint32_t)rx[1] << 8) | rx[2]) >> 7) & 0x3FFu;
}

/* Time two wraps of the scan count.  Measured on the PicoCalc: 484 lines in
 * 17,014 us, 58.8 Hz. */
static void lcd_scan_calibrate(void) {
    uint32_t max = 0, wraps = 0, t_wrap[2] = { 0, 0 };
    uint32_t prev = lcd_read_scanline();
    uint32_t t0 = time_us_32();
    while (wraps < 2 && time_us_32() - t0 < 60000u) {
        uint32_t v = lcd_read_scanline();
        if (v > max) max = v;
        if (v + 200u < prev) t_wrap[wraps++] = time_us_32();
        prev = v;
    }
    uint32_t period = t_wrap[1] - t_wrap[0];
    uint32_t baud = spi_get_baudrate(LCD_SPI_PORT);
    s_row_us = (uint32_t)((LCD_W * 2u * 8u * 1000000ull) / baud) * 11u / 10u + 2u;
    if (wraps == 2 && max >= 320u && max < 1000u && period > 8000u && period < 40000u) {
        s_scan_lines = max + 1u;
        s_scan_period_us = period;
        s_scan_sync = true;
        printf("LCD: panel scan %lu lines in %lu us; writes timed to it (row %lu us)\n",
               (unsigned long)s_scan_lines, (unsigned long)period, (unsigned long)s_row_us);
    } else {
        printf("LCD: panel scan not readable (wraps=%lu max=%lu); writes not timed\n",
               (unsigned long)wraps, (unsigned long)max);
    }
}

/* Wait until a write of n rows in runs runs, starting at panel row first,
 * will not be crossed by the scan.  Gives up after two refreshes. */
static void __not_in_flash_func(lcd_wait_scan)(uint32_t first, uint32_t n, uint32_t runs) {
    const uint32_t lines = s_scan_lines;
    uint32_t d_us = n * s_row_us + runs * RUN_OVERHEAD_US;
    /* Lines the scan gains on the write front over the write. */
    int32_t gain = (int32_t)((uint64_t)d_us * lines / s_scan_period_us) - (int32_t)(d_us / s_row_us);
    int32_t limit = (int32_t)lines - SCAN_MARGIN - gain;
    if (limit < 0) limit = 0;
    uint32_t t0 = time_us_32();
    while (time_us_32() - t0 < 2u * s_scan_period_us) {
        uint32_t d = (lcd_read_scanline() + lines - first % lines) % lines;
        if ((int32_t)d <= limit) break;
    }
    lcd_wait_us += time_us_32() - t0;
}

/*
 * Send the rows of one frame that differ from what the panel shows.  Each
 * run of changed rows gets its own window, open-ended at the bottom: CASET,
 * RASET from the run's first row, RAMWR, then rows until an unchanged one.
 * A new window costs a few microseconds, against about 80 us for a row, so
 * a run is broken at every unchanged row.  The CPU converts the next row
 * while DMA sends the last.
 *
 * Two passes: the first hashes every row, so the second knows before it
 * starts how much it will send, which is what lcd_wait_scan() needs.
 */
static uint32_t s_new_sum[256];

static void __not_in_flash_func(lcd_push_frame)(const uint8_t *fb) {
    const int y0 = (LCD_H - 256) / 2;
    bool force = s_force;
    s_force = false;

    uint32_t n = 0, runs = 0;
    int first = -1;
    bool prev = false;
    for (int y = 0; y < 256; y++) {
        uint32_t h = row_hash(fb + y * 320);
        s_new_sum[y] = h;
        bool changed = force || h != s_row_sum[y];
        if (changed) {
            n++;
            if (first < 0) first = y;
            if (!prev) runs++;
        }
        prev = changed;
    }
    if (!n) return;
    if (s_scan_sync) lcd_wait_scan((uint32_t)(y0 + first), n, runs);

    bool open = false;
    int k = 0;
    for (int y = first; y < 256; y++) {
        if (!force && s_new_sum[y] == s_row_sum[y]) {
            if (open) {
                lcd_dma_wait();
                lcd_cs(1);
                open = false;
            }
            continue;
        }
        s_row_sum[y] = s_new_sum[y];
        row_convert(s_rows[k], fb + y * 320);
        if (!open) {
            lcd_window(0, (uint16_t)(y0 + y), LCD_W - 1, (uint16_t)(y0 + 255));
            open = true;
        } else {
            lcd_dma_wait();
        }
        lcd_dma_start(s_rows[k], sizeof s_rows[k]);
        k ^= 1;
    }
    if (open) {
        lcd_dma_wait();
        lcd_cs(1);
    }
    lcd_rows_sent += n;
}

static void __not_in_flash_func(lcd_core1_run)(void) {
    lcd_panel_init();
    lcd_clear();
    lcd_dma_init();
    lcd_scan_calibrate();
    printf("LCD: panel initialised on core 1\n");

    for (;;) {
        uint32_t save = spin_lock_blocking(s_lock);
        uint8_t *fb = s_latest;
        if (fb) {
            s_sending = fb;
            s_latest  = NULL;
        }
        spin_unlock(s_lock, save);
        if (!fb) {
            tight_loop_contents();
            continue;
        }
        lcd_push_frame(fb);
        save = spin_lock_blocking(s_lock);
        s_sending = NULL;
        spin_unlock(s_lock, save);
        lcd_frames_sent++;
    }
}

/* ── graphics_* interface ─────────────────────────────────────────────────── */

void graphics_init(g_out out) {
    (void)out;
    s_lock = spin_lock_init(spin_lock_claim_unused(true));
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

/* Core 0, once a frame, after the frame in buffer is complete. */
void graphics_set_buffer(uint8_t *buffer) {
    uint32_t save = spin_lock_blocking(s_lock);
    s_latest = buffer;
    spin_unlock(s_lock, save);
    s_buffer = buffer;
    lcd_present_count++;
}

uint8_t *lcd_begin_frame(uint8_t *a, uint8_t *b) {
    uint32_t save = spin_lock_blocking(s_lock);
    uint8_t *d;
    if (s_sending == a)      d = b;
    else if (s_sending == b) d = a;
    else                     d = (s_latest == a) ? b : a;
    /* Core 1 is sending the other buffer and has not taken this frame yet;
     * it is about to be drawn over, so it is never shown. */
    if (s_latest == d) s_latest = NULL;
    spin_unlock(s_lock, save);
    return d;
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
    s_force = true;   /* the same indices now look different: resend everything */
}
uint32_t graphics_get_palette(uint8_t i) { return s_palette[i]; }
void graphics_set_bgcolor(uint32_t color888) { (void)color888; }
void graphics_restore_sync_colors(void) {}

void graphics_set_crt_active(bool active) { s_crt_active = active; }
bool graphics_get_crt_active(void)        { return s_crt_active; }
void graphics_set_greyscale(bool active)  { s_greyscale = active; }
bool graphics_get_greyscale(void)         { return s_greyscale; }
