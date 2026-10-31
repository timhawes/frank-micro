/*
 * frank-micro — BBC Micro for RP2350 (b-em port)
 * frank_gui.c — bridges b-em's X_GUI raw-row video path and audio output
 *               onto the frank HDMI 8-bit palette framebuffer.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-micro
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * b-em (built with -DX_GUI -DSINGLE_CORE) rasterises each displayed scanline
 * synchronously on core0 into a flat 640-pixel RGB555 row (scanline_buffer->
 * row0).  We convert each row to an 8-bit BBC palette index and write it,
 * 2:1 horizontally downscaled, into the 320x256 framebuffer that the frank
 * HDMI encoder scans out from core1.
 */
#include <stdint.h>
#include <string.h>
#include "pico.h"

#include "x_gui.h"        /* struct scanvideo_scanline_buffer, RGB555 macros   */
#include "board_config.h" /* MICRO_FB_* geometry for the selected driver       */
#include "HDMI.h"         /* graphics_set_buffer, FRAMEBUFFER_PIXEL            */

#include "frank_gui.h"
#include "frank_ui.h"
#include "frank_screenshot.h"

/* The 8-bit framebuffer scanned out by the video driver.  Geometry comes from
 * board_config.h, which sets it from the selected driver. */
#define FB_W  MICRO_FB_WIDTH
#define FB_H  MICRO_FB_HEIGHT
#define FB_BBC_W  MICRO_FB_BBC_W    /* engine pixels kept per row */
#define FB_X0     MICRO_FB_X_OFFSET /* where they start in the row */

#ifdef HDMI_HSTX
/*
 * Single-buffered, because 720x256 is 180 KB and there is no room for a
 * second copy: keeping the 640 engine pixels is what fixes the 80-column
 * blur, and it costs the same memory two 320-wide buffers used to.
 *
 * SCREEN[0] and SCREEN[1] therefore name the same memory, so every existing
 * SCREEN[current_buffer] site keeps working and the flip becomes a no-op.
 * Tearing is handled instead by phase-locking the emulator frame to the
 * display's vertical blanking; see the pacer in frank_platform.c.
 */
static uint8_t __attribute__((aligned(4))) screen_mem[1][FB_W * FB_H];
uint8_t *SCREEN[2] = { screen_mem[0], screen_mem[0] };
#else
static uint8_t __attribute__((aligned(4))) screen_mem[2][FB_W * FB_H];
uint8_t *SCREEN[2] = { screen_mem[0], screen_mem[1] };
#endif
volatile uint32_t current_buffer = 0;

/* One reusable 640-wide RGB555 working row (+ a second for double-height). */
#ifdef VIDEO_LCD_SPI
/* Word-aligned so clear_row() can use 32-bit stores. */
static uint16_t __attribute__((aligned(4))) row_work0[640];
static uint16_t __attribute__((aligned(4))) row_work1[640];
#else
static uint16_t row_work0[640];
static uint16_t row_work1[640];
#endif
static struct scanvideo_scanline_buffer s_buffer;

/*
 * Frame/scanline bookkeeping, faithfully matching the upstream x_gui:
 *   - scanline_number counts begin_scanline() calls 1..256 within a frame.
 *   - frame_number_part increments by 0x10000 at the start of each frame.
 *   - scanline_id = frame_number_part | scanline_number.
 * display.c's per-line catch-up logic uses scanvideo_frame_number(scanline_id)
 * to detect a new frame and scanvideo_scanline_number() for the line position,
 * padding each frame to exactly 256 emitted scanlines.  Encoding the frame
 * number is essential: without it, when a program reprograms the CRTC (e.g.
 * the Prince of Persia cut scenes) the scanline accounting desyncs and the
 * frame never completes, freezing the display while the CPU keeps running.
 */
#ifdef HDMI_HSTX
/* Clean rasteriser frames still owed before a pending screenshot is taken,
 * and whether the current request has already armed that countdown.
 * See the comment at the capture site in x_gui_end_scanline(). */
static int      s_shot_settle;
static bool     s_shot_armed;
#endif
static int      s_scanline_number;     /* 0..256 within the current frame */
static uint32_t s_frame_number_part;   /* high 16 bits of scanline_id      */

/*
 * Map an RGB555 pixel to a BBC physical-colour index 0..7.
 * BBC colours are full-on/off per channel; the ULA palette also produces
 * teletext anti-alias blends which we snap to the nearest primary.
 */
static inline uint8_t rgb555_to_bbc(uint16_t p) {
    uint8_t r = PICO_SCANVIDEO_R5_FROM_PIXEL(p);
    uint8_t g = PICO_SCANVIDEO_G5_FROM_PIXEL(p);
    uint8_t b = PICO_SCANVIDEO_B5_FROM_PIXEL(p);
    return (uint8_t)((r >= 16 ? 1 : 0) | (g >= 16 ? 2 : 0) | (b >= 16 ? 4 : 0));
}

/* Luminance proxy: brightest channel of an RGB555 pixel (0..31). */
static inline uint8_t lum5(uint16_t p) {
    uint8_t r = PICO_SCANVIDEO_R5_FROM_PIXEL(p);
    uint8_t g = PICO_SCANVIDEO_G5_FROM_PIXEL(p);
    uint8_t b = PICO_SCANVIDEO_B5_FROM_PIXEL(p);
    uint8_t m = r > g ? r : g;
    return m > b ? m : b;
}

/*
 * Render one MODE 7 teletext scanline pixel-perfectly.
 *
 * The engine emits each 40-column teletext line into a 640-wide row: a fixed
 * 16 sub-pixels per character (char c → src[c*16 .. c*16+15]).  Those 16
 * sub-pixels are the engine's interpolation of the 6-column SAA5050 glyph cell
 * (the original 6 columns doubled to 12, then linearly resampled 12→16).
 *
 * Tracing that 12→16 resample shows the six original font columns reappear at
 * full intensity exactly at sub-pixel offsets {0,2,5,8,11,14}; every other
 * offset is an interpolated blend (or character-rounding bleed in the other
 * field).  Sampling only those six points therefore recovers the original
 * SAA5050 glyph with no blur, giving uniform 1-px stems for every character.
 * We place the 6 columns 1:1 into the 8-px output cell with a 1-px margin
 * either side.  Colours come from the sampled pixels, so coloured teletext and
 * coloured backgrounds are preserved.
 */
#define TELETEXT_INK_LUM 16

/* Per-scanline hooks run from RAM on the PicoCalc, where core 1 is also
 * fetching from flash.  Every other build keeps them where they were. */
#ifdef VIDEO_LCD_SPI
#define FRANK_GUI_HOT(f) __not_in_flash_func(f)
#else
#define FRANK_GUI_HOT(f) f
#endif   /* original column is full-on (≈31) or off (0) */

#if !defined(HDMI_HSTX) && !defined(VIDEO_LCD_SPI)
static void blit_row_teletext(const uint16_t *src, uint8_t *dst) {
    /* Sub-pixel offset of each original glyph column within the 16-wide cell. */
    static const uint8_t col_off[6] = { 0, 2, 5, 8, 11, 14 };

    for (int c = 0; c < FB_BBC_W / 8; c++) {   /* 40 character cells */
        const uint16_t *cell = src + c * 16;
        uint8_t *o = dst + c * 8;

        /* Cell background = darkest sampled column (covers coloured bg). */
        uint16_t bgpx = cell[col_off[0]];
        uint8_t  bglum = lum5(cell[col_off[0]]);
        for (int i = 1; i < 6; i++) {
            uint8_t l = lum5(cell[col_off[i]]);
            if (l < bglum) { bglum = l; bgpx = cell[col_off[i]]; }
        }
        uint8_t bg = rgb555_to_bbc(bgpx);

        o[0] = bg;                              /* left inter-char margin */
        for (int i = 0; i < 6; i++) {
            uint16_t px = cell[col_off[i]];
            o[i + 1] = (lum5(px) >= TELETEXT_INK_LUM && lum5(px) > bglum)
                       ? rgb555_to_bbc(px) : bg;
        }
        o[7] = bg;                              /* right inter-char margin */
    }
}
#elif defined(VIDEO_LCD_SPI)
/*
 * The same renderer for the PicoCalc, built on a lookup table.
 *
 * Profiled on the Master's MODE 7 screen, the version above took about half
 * of core 0's time.  It works out lum5() up to three times per sample and
 * rgb555_to_bbc() up to twice, from flash.  Here each sample costs one load:
 * s_px[] maps a pixel to (lum5 << 3) | bbc index.  The pixel's unused bit 5
 * is squeezed out to make a 15-bit key, so the table is 32 KB of RAM.
 *
 * The algorithm is unchanged, so the output is byte-identical to the version
 * above, including which sample wins a tie for the darkest.  Only the
 * PicoCalc uses it because the other boards cannot be tested here; it is
 * portable to them as it stands.
 */
static uint8_t s_px[32768];
static bool    s_px_ready;

static inline uint32_t px_key(uint16_t p) {
    return ((uint32_t)(p >> 1) & 0x7FE0u) | (p & 0x1Fu);
}

static void s_px_fill(void) {
    for (uint32_t k = 0; k < 32768u; k++) {
        uint16_t p = (uint16_t)(((k & 0x7FE0u) << 1) | (k & 0x1Fu));
        s_px[k] = (uint8_t)((lum5(p) << 3) | rgb555_to_bbc(p));
    }
    s_px_ready = true;
}

static void __not_in_flash_func(blit_row_teletext)(const uint16_t *src, uint8_t *dst) {
    if (!s_px_ready) s_px_fill();

    for (int c = 0; c < FB_BBC_W / 8; c++) {   /* 40 character cells */
        const uint16_t *cell = src + c * 16;
        uint8_t *o = dst + c * 8;

        /* A cell whose six samples are the same pixel is all background:
         * nothing is brighter than the darkest sample.  Most of a teletext
         * screen is such cells. */
        const uint16_t p0 = cell[0];
        if (cell[2] == p0 && cell[5] == p0 && cell[8] == p0 &&
            cell[11] == p0 && cell[14] == p0) {
            uint32_t w = (s_px[px_key(p0)] & 7u) * 0x01010101u;
            ((uint32_t *)o)[0] = w;     /* dst rows and cells are 4-aligned */
            ((uint32_t *)o)[1] = w;
            continue;
        }

        uint8_t v[6] = {
            s_px[px_key(cell[0])],  s_px[px_key(cell[2])],  s_px[px_key(cell[5])],
            s_px[px_key(cell[8])],  s_px[px_key(cell[11])], s_px[px_key(cell[14])],
        };

        /* Cell background = darkest sample, the first one on a tie. */
        uint8_t bgv = v[0];
        for (int i = 1; i < 6; i++)
            if ((v[i] >> 3) < (bgv >> 3)) bgv = v[i];
        const uint8_t bg = bgv & 7u, bglum = bgv >> 3;

        o[0] = bg;
        for (int i = 0; i < 6; i++) {
            uint8_t l = v[i] >> 3;
            o[i + 1] = (l >= TELETEXT_INK_LUM && l > bglum) ? (v[i] & 7u) : bg;
        }
        o[7] = bg;
    }
}
#endif /* teletext renderer */

/*
 * Convert a 640-wide RGB555 row into one 8bpp framebuffer row.
 *
 * On the PIO drivers the framebuffer is 320 wide, so every other pixel is
 * point-sampled.  That is lossless for the 40-column modes, where the engine
 * doubles each pixel, and MODE 7 needs the dedicated renderer above because
 * point-sampling its 16-pixel cells lands on interpolated blend columns.
 *
 * Under HSTX the framebuffer is wide enough to take all 640 pixels, so the
 * row is copied 1:1 into the middle of it and neither special case applies:
 * MODE 0 and MODE 3 keep their 80 columns, and MODE 7's cells arrive whole.
 */
static void FRANK_GUI_HOT(blit_row)(const uint16_t *src, int y, bool teletext) {
    if (y < 0 || y >= FB_H) return;
    uint8_t *dst = SCREEN[current_buffer] + y * FB_W + FB_X0;
#ifdef HDMI_HSTX
    (void)teletext;
    /*
     * The framebuffer is single-buffered here, and this row is written ahead
     * of the scanout beam so the picture does not tear.  That has a
     * consequence for the F11 and F12 overlay: anything composited on top
     * after the rows are written is erased by the next frame's blit before
     * the beam ever reaches it, and the overlay decays row by row as the two
     * frame rates drift against each other.
     *
     * So the blit yields the span the overlay owns instead of repainting it.
     * frank_ui_render() keeps that span painted, and the picture around it
     * carries on updating normally.
     */
    int ox0, ox1;
    if (frank_ui_row_span(y, FB_W, &ox0, &ox1)) {
        int s0 = ox0 - FB_X0, s1 = ox1 - FB_X0;
        if (s0 < 0) s0 = 0; else if (s0 > FB_BBC_W) s0 = FB_BBC_W;
        if (s1 < 0) s1 = 0; else if (s1 > FB_BBC_W) s1 = FB_BBC_W;
        for (int x = 0; x < s0; x++)
            dst[x] = FRAMEBUFFER_PIXEL(rgb555_to_bbc(src[x]));
        for (int x = s1; x < FB_BBC_W; x++)
            dst[x] = FRAMEBUFFER_PIXEL(rgb555_to_bbc(src[x]));
        return;
    }
    for (int x = 0; x < FB_BBC_W; x++)
        dst[x] = FRAMEBUFFER_PIXEL(rgb555_to_bbc(src[x]));
#else
#ifdef VIDEO_LCD_SPI
    /*
     * The panel receives a frame over about three emulated frames, so this
     * buffer is usually still being sent while it is repainted.  An overlay
     * composited at the end of the frame was missing from the rows repainted
     * mid-send, and flickered.  So, as under HSTX, the span the overlay owns
     * is left alone: the row is drawn to a scratch line and only the part
     * outside the span is copied in.
     */
    int ox0, ox1;
    if (frank_ui_row_span(y, FB_W, &ox0, &ox1)) {
        static uint8_t __attribute__((aligned(4))) line[FB_BBC_W];
        if (teletext) {
            blit_row_teletext(src, line);
        } else {
            if (!s_px_ready) s_px_fill();
            for (int x = 0; x < FB_BBC_W; x++)
                line[x] = s_px[px_key(src[x * 2])] & 7u;
        }
        int s0 = ox0 - FB_X0, s1 = ox1 - FB_X0;
        if (s0 < 0) s0 = 0; else if (s0 > FB_BBC_W) s0 = FB_BBC_W;
        if (s1 < 0) s1 = 0; else if (s1 > FB_BBC_W) s1 = FB_BBC_W;
        for (int x = 0; x < s0; x++) dst[x] = line[x];
        for (int x = s1; x < FB_BBC_W; x++) dst[x] = line[x];
        return;
    }
#endif
    if (teletext) {
        blit_row_teletext(src, dst);
    } else {
#ifdef VIDEO_LCD_SPI
        /* The same point-sampling, through the lookup table: s_px[] & 7 is
         * rgb555_to_bbc() of the pixel by construction. */
        if (!s_px_ready) s_px_fill();
        for (int x = 0; x < FB_BBC_W; x++)
            dst[x] = s_px[px_key(src[x * 2])] & 7u;
#else
        for (int x = 0; x < FB_BBC_W; x++)
            dst[x] = rgb555_to_bbc(src[x * 2]);
#endif
    }
#endif
}

#ifdef VIDEO_LCD_SPI
static void __not_in_flash_func(clear_row)(uint16_t *row) {
    uint32_t *w = (uint32_t *)row;
    for (int i = 0; i < 640 / 2; i += 8) {
        w[i] = 0; w[i + 1] = 0; w[i + 2] = 0; w[i + 3] = 0;
        w[i + 4] = 0; w[i + 5] = 0; w[i + 6] = 0; w[i + 7] = 0;
    }
}
#endif

/* ── x_gui video hooks called from b-em display.c ────────────────────────── */

int x_gui_init(void) {
    return 0;
}

/* display.c sets buffer->double_height before each scanline in teletext. */
struct scanvideo_scanline_buffer *FRANK_GUI_HOT(x_gui_begin_scanline)(void) {
    if (s_scanline_number++ == 0) {
        /* Start of a new frame: draw into the back buffer, bump frame number. */
        current_buffer ^= 1u;
        s_frame_number_part += 0x10000u;
    }
    s_buffer.row0 = row_work0;
    s_buffer.row1 = s_buffer.double_height ? row_work1 : NULL;
    s_buffer.scanline_id = s_frame_number_part | (uint32_t)s_scanline_number;
    /* Clear the work row so undriven pixels are black. */
#ifdef VIDEO_LCD_SPI
    /* memset() here was 12% of core 0 in MODE 7; word stores from RAM. */
    clear_row(row_work0);
    if (s_buffer.row1) clear_row(row_work1);
#else
    memset(row_work0, 0, sizeof(row_work0));
    if (s_buffer.row1) memset(row_work1, 0, sizeof(row_work1));
#endif
    return &s_buffer;
}

void FRANK_GUI_HOT(x_gui_end_scanline)(struct scanvideo_scanline_buffer *buffer) {
    /* scanline_number is 1-based here (post-increment in begin); the row index
     * on screen is scanline_number-1.  double_height is set by display.c only
     * for MODE 7 teletext scanlines, which need the box-filtered downscale. */
    blit_row(buffer->row0, s_scanline_number - 1, buffer->double_height);
    if (s_scanline_number >= FB_H) {
        s_scanline_number = 0;
        /* Deferred screenshot: capture the clean BBC frame before any overlay
         * is drawn (raised by the PrintScreen key or the F12 menu). */
#ifdef HDMI_HSTX
        /*
         * Arm the settle countdown on the rising edge of the request, before
         * anything looks at whether the overlay is showing.
         *
         * It cannot be armed later, from the overlay's own visibility: the
         * keypress that requests a screenshot from the F12 menu also closes
         * that menu, so by the time this code runs the overlay already reads
         * as hidden even though the frame in progress is still half full of
         * it. Conditioning on visibility captured the mixed frame, and only
         * appeared to work when a leftover toast happened to be counting
         * down and armed the countdown by accident.
         */
        if (g_frank_screenshot_pending && !s_shot_armed) {
            s_shot_armed = true;
            s_shot_settle = 2;
        } else if (!g_frank_screenshot_pending) {
            s_shot_armed = false;
        }
#endif
        if (g_frank_screenshot_pending) {
#ifdef HDMI_HSTX
            /*
             * Single-buffered: the overlay is composited into this same
             * framebuffer and blit_row() yields the rows it owns, so those
             * rows keep the overlay until it closes and a whole rasteriser
             * frame has repainted them.
             *
             * Skipping one frame is not enough, because the rasteriser's
             * frame boundary does not line up with the once-per-frame
             * keyboard poll: s_scanline_number wraps somewhere inside
             * m6502_exec, so the frame on which the menu closes is part old
             * and part new. This frame is that mixed one; two more clean
             * frames follow before the capture. Staying visible, which
             * includes a toast being up, re-arms the wait.
             */
            if (frank_ui_is_visible()) {
                s_shot_settle = 2;
            } else if (s_shot_settle > 0) {
                --s_shot_settle;
            } else
#endif
            {
                g_frank_screenshot_pending = false;
                int rc = frank_screenshot_save(SCREEN[current_buffer]);
                frank_ui_toast(rc == 0 ? "Screenshot saved" : "Screenshot failed");
            }
        }
        /* Overlay the settings/browser UI onto the freshly-rendered frame. */
        if (frank_ui_is_visible())
            frank_ui_render(SCREEN[current_buffer], FB_W, FB_H);
        /* Frame complete — present it to the HDMI encoder. */
        graphics_set_buffer(SCREEN[current_buffer]);
    }
}

void x_gui_refresh_menu_display(void) {
}
