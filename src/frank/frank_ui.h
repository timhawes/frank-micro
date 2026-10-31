/*
 * frank-micro — BBC Micro for RP2350 (b-em port)
 * frank_ui.h — settings overlay + disc browser UI.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-micro
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * F12 → Settings screen, F11 → Media browser (Drive A / Drive B).
 */
#ifndef FRANK_UI_H
#define FRANK_UI_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Virtual key codes routed to the UI (set by frank_keyboard). */
#define FRANK_KS_Escape    0xFF1B
#define FRANK_KS_Return    0xFF0D
#define FRANK_KS_Up        0xFF52
#define FRANK_KS_Down      0xFF54
#define FRANK_KS_Left      0xFF51
#define FRANK_KS_Right     0xFF53
#define FRANK_KS_Page_Up   0xFF55
#define FRANK_KS_Page_Down 0xFF56
#define FRANK_KS_BackSpace 0xFF08
#define FRANK_KS_Delete    0xFFFF

/* Call once during platform init (installs the UI palette). */
void frank_ui_init(void);

/* True while the overlay or a toast is visible. */
bool frank_ui_is_visible(void);

/* True when UI panels are open and should capture keyboard input. */
bool frank_ui_wants_keys(void);

/* Toggle the settings overlay (mapped to F12). */
void frank_ui_toggle(void);

/* Open the media (drive) selection menu (mapped to F11). */
void frank_ui_open_disk_menu(void);

/* Non-blocking key event. Returns true if consumed by the UI. */
bool frank_ui_handle_key(unsigned int ks);

/* Render the overlay onto the 8-bpp framebuffer.
 * stride = row width in bytes (320), height = number of rows (256). */
void frank_ui_render(uint8_t *fb, int stride, int height);

/* Show a transient status message at the top of the screen for ~2 seconds.
 * Used to confirm actions such as saving a screenshot. */
void frank_ui_toast(const char *msg);

#if defined(HDMI_HSTX) || defined(VIDEO_LCD_SPI)
/* Horizontal span of framebuffer row `y` that the overlay currently owns, in
 * framebuffer coordinates, or false if the overlay does not cover that row.
 *
 * The PicoCalc's LCD path needs it too: core 1 takes about three emulated
 * frames to send one to the panel, so core 0 is already repainting the
 * buffer it is sending, and an overlay composited only at the end of the
 * frame flickered.  Yielding the span keeps it painted in both buffers.
 *
 * The single-buffered HSTX path needs it for a related reason. There the emulator writes
 * every row ahead of the scanout beam, so anything composited on top after
 * the rows are written is erased before it is ever displayed. The blit skips
 * this span rather than repainting it, and frank_ui_render() keeps it
 * painted. */
bool frank_ui_row_span(int y, int stride, int *x0, int *x1);
#endif

#ifdef __cplusplus
}
#endif

#endif /* FRANK_UI_H */
