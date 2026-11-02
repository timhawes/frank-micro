/*
 * frank-micro — BBC Micro for RP2350 (b-em port)
 * frank_ui.c — settings overlay + media menu + disc browser state machine.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-micro
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * F12 → Settings screen (ESC or "Back to BBC" closes)
 * F11 → Media menu (Drive A / Drive B select + eject)
 */

#include "frank_ui.h"
#include "frank_settings.h"
#include "frank_loader.h"
#include "frank_disc.h"
#include "frank_screenshot.h"
#include "ui_draw.h"
#include "crash_handler.h"
#include "board_config.h"
#ifdef VIDEO_LCD_SPI
#include "frank_gui.h"       /* SCREEN[], for the per-buffer redraw bits */
#endif

#include <string.h>
#include <stdio.h>

/* ── layout ────────────────────────────────────────────────────────────── */
#define SCREEN_H 256
/* The window is sized from the framebuffer rather than fixed at the 320-wide
 * PIO geometry, so the HSTX build's 720-wide screen gets a centred window
 * instead of a small one in the corner.  Everything inside is laid out from
 * content_w(), so the extra room shows up as longer filenames in the browser
 * rather than as stretched chrome. */
#define WIN_W   (MICRO_FB_BBC_W - 20)
#define WIN_X   (MICRO_FB_X_OFFSET + 10)
#define WIN_PAD 6

/* Window box is sized to its content and centred vertically each render. */
static int s_win_y = 16;
static int s_win_h = 224;

/* Size the window to `content_h` pixels of body and centre it on screen. */
static void layout_window(int content_h) {
    s_win_h = UI_HEADER_H + WIN_PAD + content_h + WIN_PAD + UI_LINE_H + WIN_PAD;
    if (s_win_h > SCREEN_H) s_win_h = SCREEN_H;
    s_win_y = (SCREEN_H - s_win_h) / 2;
    if (s_win_y < 0) s_win_y = 0;
}

/* Settings rows */
#define SETTINGS_SCREENSHOT_ROW FRANK_SETTING_COUNT
#define SETTINGS_APPLY_ROW      (FRANK_SETTING_COUNT + 1)
#define SETTINGS_BACK_ROW       (FRANK_SETTING_COUNT + 2)
#define SETTINGS_TOTAL_ROWS     (FRANK_SETTING_COUNT + 3)
#define SETTINGS_VISIBLE_ROWS 14

#define DISK_VISIBLE_ROWS 13
#define MENU_ROWS 2

/* ── state ─────────────────────────────────────────────────────────────── */
typedef enum {
    UI_HIDDEN = 0,
    UI_SETTINGS,
    UI_SETTINGS_CONFIRM,
    UI_DISK_MENU,
    UI_DISK_BROWSER,
} ui_state_t;

static ui_state_t s_state            = UI_HIDDEN;
static int        s_setting_row      = 0;
static int        s_settings_scroll  = 0;

static int        s_menu_row         = 0;   /* 0=Drive A, 1=Drive B */

static int        s_disk_drive       = 0;
static int        s_disk_row         = 0;
static int        s_disk_scroll      = 0;
static char       s_disk_msg[64]     = "";

static char       s_toast[64]        = "";
static int        s_toast_frames     = 0;

/* Dynamic browser-row helpers (depend on s_disk_drive). */
static bool disk_has_eject(void) { return frank_disc_name(s_disk_drive) != NULL; }
static int  disk_dotdot_row(void)   { return disk_has_eject() ? 1 : 0; }
static int  disk_entry_offset(void) { return disk_has_eject() ? 2 : 1; }
static int  disk_total_rows(void)   { return disk_entry_offset() + g_frank_disk_entry_count; }

/*
 * Redraw gating, HSTX only.
 *
 * On the single-buffered path the overlay lives in the same memory the
 * scanout beam is reading, and drawing it costs roughly as long as 20 raster
 * lines.  Redrawing an unchanged overlay every frame therefore drags a tear
 * band across it once per beat between the emulator's frame rate and the
 * display's, which is slow and very visible.  Draw only when something
 * actually changed.
 *
 * The PIO drivers composite into a back buffer, where a redraw is free of
 * that hazard, so they keep redrawing every frame and their binaries are
 * unchanged.
 */
#ifdef HDMI_HSTX
static bool s_dirty = true;
static void frank_ui_mark_dirty(void) { s_dirty = true; }
#elif defined(VIDEO_LCD_SPI)
/* The PicoCalc's LCD path redraws only on a change, as under HSTX, but it has
 * two buffers, so a change has to reach both.  One bit per buffer.  They do
 * not strictly alternate: the emulator draws into whichever one core 1 is
 * not sending (lcd_begin_frame()), so a bit is cleared only by a render
 * into that buffer. */
static unsigned s_dirty = 3u;
static void frank_ui_mark_dirty(void) { s_dirty = 3u; }
#else
#define frank_ui_mark_dirty() ((void)0)
#endif

/* ── public ────────────────────────────────────────────────────────────── */

void frank_ui_init(void) {
    ui_draw_install_palette();
}

bool frank_ui_is_visible(void) {
    return s_state != UI_HIDDEN || s_toast_frames > 0;
}

bool frank_ui_wants_keys(void) {
    return s_state != UI_HIDDEN;
}

void frank_ui_toast(const char *msg) {
    if (!msg) return;
    snprintf(s_toast, sizeof(s_toast), "%s", msg);
    s_toast_frames = 100;   /* ~2 s at 50 Hz */
    frank_ui_mark_dirty();
}

void frank_ui_toggle(void) {
    frank_ui_mark_dirty();
    if (s_state == UI_HIDDEN) {
        s_state           = UI_SETTINGS;
        s_setting_row     = 0;
        s_settings_scroll = 0;
    } else {
        s_state = UI_HIDDEN;
    }
}

void frank_ui_open_disk_menu(void) {
    s_menu_row = 0;
    s_state    = UI_DISK_MENU;
    frank_ui_mark_dirty();
}

static void open_browser_for(int drv) {
    s_disk_drive  = drv;
    s_disk_row    = 0;
    s_disk_scroll = 0;
    s_disk_msg[0] = '\0';
    crash_handler_feed();
    frank_disk_rescan();
    s_state = UI_DISK_BROWSER;
}

/* ── key handling ──────────────────────────────────────────────────────── */

static bool handle_settings_page(unsigned int ks) {
    bool handled = false;

    switch (ks) {
        case FRANK_KS_Escape:
            s_state = UI_HIDDEN;
            return true;

        case FRANK_KS_Up:
            if (--s_setting_row < 0) s_setting_row = SETTINGS_TOTAL_ROWS - 1;
            handled = true; break;

        case FRANK_KS_Down:
            if (++s_setting_row >= SETTINGS_TOTAL_ROWS) s_setting_row = 0;
            handled = true; break;

        case FRANK_KS_Left:
            if (s_setting_row < FRANK_SETTING_COUNT)
                frank_settings_step((frank_setting_id_t)s_setting_row, -1);
            return true;

        case FRANK_KS_Right:
            if (s_setting_row < FRANK_SETTING_COUNT)
                frank_settings_step((frank_setting_id_t)s_setting_row, +1);
            return true;

        case FRANK_KS_Return:
            if (s_setting_row == SETTINGS_SCREENSHOT_ROW) {
                g_frank_screenshot_pending = true;
                s_state = UI_HIDDEN;
            } else if (s_setting_row == SETTINGS_APPLY_ROW) {
                if (g_frank_settings_dirty)
                    s_state = UI_SETTINGS_CONFIRM;
                else {
                    s_state = UI_HIDDEN;
                    frank_settings_do_restart();
                }
            } else if (s_setting_row == SETTINGS_BACK_ROW) {
                s_state = UI_HIDDEN;
            } else if (s_setting_row < FRANK_SETTING_COUNT) {
                frank_settings_step((frank_setting_id_t)s_setting_row, +1);
            }
            return true;

        default:
            return false;
    }

    if (s_setting_row < s_settings_scroll)
        s_settings_scroll = s_setting_row;
    else if (s_setting_row >= s_settings_scroll + SETTINGS_VISIBLE_ROWS)
        s_settings_scroll = s_setting_row - SETTINGS_VISIBLE_ROWS + 1;

    return handled;
}

static bool handle_settings_confirm(unsigned int ks) {
    switch (ks) {
        case FRANK_KS_Return:
            s_state = UI_HIDDEN;
            frank_settings_do_restart();
            return true;
        case FRANK_KS_Escape:
            s_state = UI_SETTINGS;
            return true;
        default:
            return false;
    }
}

static bool handle_disk_menu_key(unsigned int ks) {
    switch (ks) {
        case FRANK_KS_Escape:
            s_state = UI_HIDDEN;
            return true;
        case FRANK_KS_Up:
            if (s_menu_row > 0) --s_menu_row;
            return true;
        case FRANK_KS_Down:
            if (s_menu_row < MENU_ROWS - 1) ++s_menu_row;
            return true;
        case FRANK_KS_Return:
        case FRANK_KS_Right:
            open_browser_for(s_menu_row);   /* 0=A, 1=B */
            return true;
        default:
            return false;
    }
}

static bool handle_disk_browser_key(unsigned int ks) {
    int total = disk_total_rows();
    switch (ks) {
        case FRANK_KS_Escape:
        case FRANK_KS_Left:
            s_state = UI_DISK_MENU;
            return true;

        case FRANK_KS_Up:
            if (s_disk_row > 0) {
                --s_disk_row;
                if (s_disk_row < s_disk_scroll) s_disk_scroll = s_disk_row;
            }
            return true;

        case FRANK_KS_Down:
            if (s_disk_row < total - 1) {
                ++s_disk_row;
                if (s_disk_row >= s_disk_scroll + DISK_VISIBLE_ROWS)
                    s_disk_scroll = s_disk_row - DISK_VISIBLE_ROWS + 1;
            }
            return true;

        case FRANK_KS_Page_Up:
            s_disk_row -= DISK_VISIBLE_ROWS;
            if (s_disk_row < 0) s_disk_row = 0;
            if (s_disk_row < s_disk_scroll) s_disk_scroll = s_disk_row;
            return true;

        case FRANK_KS_Page_Down:
            s_disk_row += DISK_VISIBLE_ROWS;
            if (s_disk_row >= total) s_disk_row = total > 0 ? total - 1 : 0;
            if (s_disk_row >= s_disk_scroll + DISK_VISIBLE_ROWS)
                s_disk_scroll = s_disk_row - DISK_VISIBLE_ROWS + 1;
            return true;

        case FRANK_KS_BackSpace: {
            int cnt = frank_disk_enter_parent();
            s_disk_row = disk_dotdot_row(); s_disk_scroll = 0;
            snprintf(s_disk_msg, sizeof(s_disk_msg),
                     "%d item%s", cnt, cnt == 1 ? "" : "s");
            return true;
        }

        case FRANK_KS_Return:
        case FRANK_KS_Right:
            if (disk_has_eject() && s_disk_row == 0) {
                frank_disc_eject(s_disk_drive);
                snprintf(s_disk_msg, sizeof(s_disk_msg),
                         "Drive %c ejected", '0' + s_disk_drive);
                s_state = UI_DISK_MENU;
                return true;
            }
            if (s_disk_row == disk_dotdot_row()) {
                int cnt = frank_disk_enter_parent();
                s_disk_row = disk_dotdot_row(); s_disk_scroll = 0;
                snprintf(s_disk_msg, sizeof(s_disk_msg),
                         "%d item%s", cnt, cnt == 1 ? "" : "s");
                return true;
            }
            {
                int ei = s_disk_row - disk_entry_offset();
                if (ei < 0 || ei >= g_frank_disk_entry_count) return true;
                if (g_frank_disk_entries[ei].is_dir) {
                    int cnt = frank_disk_enter_subdir(g_frank_disk_entries[ei].name);
                    s_disk_row = disk_dotdot_row(); s_disk_scroll = 0;
                    snprintf(s_disk_msg, sizeof(s_disk_msg),
                             "%d item%s", cnt, cnt == 1 ? "" : "s");
                } else {
                    char path[FRANK_DISK_PATH_LEN];
                    frank_disk_entry_path(ei, path, sizeof(path));
                    crash_handler_feed();
                    if (frank_disc_mount(s_disk_drive, path) == 0) {
                        snprintf(s_toast, sizeof(s_toast), "Drive %c: mounted",
                                 '0' + s_disk_drive);
                        s_toast_frames = 100;
                        /* Drive 0 → SHIFT-BREAK autoboot the disc. */
                        if (s_disk_drive == 0)
                            frank_disc_request_boot();
                        s_state = UI_HIDDEN;
                    } else {
                        snprintf(s_disk_msg, sizeof(s_disk_msg), "Failed to mount");
                    }
                }
            }
            return true;

        default:
            return false;
    }
}

bool frank_ui_handle_key(unsigned int ks) {
    /* Any key the UI acts on can move a cursor, change a value or change
     * page, so redraw after one rather than trying to work out what moved. */
    frank_ui_mark_dirty();
    switch (s_state) {
        case UI_SETTINGS:         return handle_settings_page(ks);
        case UI_SETTINGS_CONFIRM: return handle_settings_confirm(ks);
        case UI_DISK_MENU:        return handle_disk_menu_key(ks);
        case UI_DISK_BROWSER:     return handle_disk_browser_key(ks);
        default: return false;
    }
}

/* ── chrome helpers ────────────────────────────────────────────────────── */

static int content_x(void) { return WIN_X + WIN_PAD; }
static int content_y(void) { return s_win_y + UI_HEADER_H + WIN_PAD; }
static int content_w(void) { return WIN_W - 2 * WIN_PAD; }

static void draw_chrome(uint8_t *fb, int stride, const char *title) {
    ui_fill_rect  (fb, stride, WIN_X, s_win_y, WIN_W, s_win_h, UI_COLOR_BG);
    ui_draw_border(fb, stride, WIN_X, s_win_y, WIN_W, s_win_h, UI_COLOR_FG);
    ui_draw_header(fb, stride, WIN_X, s_win_y, WIN_W, title);
}

static void draw_footer(uint8_t *fb, int stride, const char *hint) {
    int fy = s_win_y + s_win_h - UI_LINE_H - WIN_PAD;
    ui_fill_rect  (fb, stride, content_x(), fy, content_w(), UI_LINE_H, UI_COLOR_BG);
    ui_draw_string(fb, stride, content_x(), fy + 1, hint, UI_COLOR_FG);
}

/* ── settings page ─────────────────────────────────────────────────────── */

static void render_settings_page(uint8_t *fb, int stride) {
    int visible = SETTINGS_TOTAL_ROWS;
    if (visible > SETTINGS_VISIBLE_ROWS) visible = SETTINGS_VISIBLE_ROWS;
    layout_window(visible * (UI_LINE_H + 1));
    draw_chrome(fb, stride, " Settings ");

    int x  = content_x();
    int y  = content_y();
    int cw = content_w();

    int last = s_settings_scroll + SETTINGS_VISIBLE_ROWS;
    if (last > SETTINGS_TOTAL_ROWS) last = SETTINGS_TOTAL_ROWS;

    for (int i = s_settings_scroll; i < last; ++i) {
        bool    sel = (i == s_setting_row);
        uint8_t bg  = sel ? UI_COLOR_ACCENT    : UI_COLOR_BG;
        uint8_t fg  = sel ? UI_COLOR_ACCENT_FG : UI_COLOR_FG;

        if (i < FRANK_SETTING_COUNT) {
            ui_fill_rect(fb, stride, x, y, cw, UI_LINE_H, bg);
            ui_draw_string(fb, stride, x + 2, y + 1,
                           frank_settings_label((frank_setting_id_t)i), fg);

            const char *val  = frank_settings_value_label((frank_setting_id_t)i);
            int         vlen = (int)strlen(val);
            int         vx   = x + cw - (vlen + 2) * UI_CHAR_W;
            if (sel) ui_draw_string(fb, stride, vx - UI_CHAR_W,          y + 1, "<", fg);
            ui_draw_string        (fb, stride, vx,                        y + 1, val, fg);
            if (sel) ui_draw_string(fb, stride, vx + vlen * UI_CHAR_W + 2, y + 1, ">", fg);

        } else if (i == SETTINGS_SCREENSHOT_ROW) {
            ui_draw_menu_item(fb, stride, x, y, cw,
                              "Take a Screenshot",
                              (cw - 4) / UI_CHAR_W, sel);
        } else if (i == SETTINGS_APPLY_ROW) {
            ui_draw_menu_item(fb, stride, x, y, cw,
                              g_frank_settings_dirty
                                  ? "Save changes and restart"
                                  : "Reset Emulator",
                              (cw - 4) / UI_CHAR_W, sel);
        } else if (i == SETTINGS_BACK_ROW) {
            ui_draw_menu_item(fb, stride, x, y, cw,
                              "Back to BBC",
                              (cw - 4) / UI_CHAR_W, sel);
        }
        y += UI_LINE_H + 1;
    }

    if (SETTINGS_TOTAL_ROWS > SETTINGS_VISIBLE_ROWS) {
        ui_draw_scrollbar(fb, stride,
                          WIN_X + WIN_W - WIN_PAD - 4,
                          content_y(),
                          SETTINGS_VISIBLE_ROWS * (UI_LINE_H + 1) - 1,
                          SETTINGS_TOTAL_ROWS,
                          SETTINGS_VISIBLE_ROWS,
                          s_settings_scroll);
    }

    draw_footer(fb, stride, "UP/DN  LEFT/RIGHT  ENTER  ESC");
}

static void render_settings_confirm(uint8_t *fb, int stride) {
    layout_window(3 * UI_LINE_H + 8);
    draw_chrome(fb, stride, " Restart required ");
    int x = content_x();
    int y = content_y();
    ui_draw_string(fb, stride, x, y,
                   "New settings will restart the BBC.", UI_COLOR_FG);
    y += UI_LINE_H + 2;
    ui_draw_string(fb, stride, x, y,
                   "All unsaved state will be lost.",  UI_COLOR_FG);
    y += UI_LINE_H + 6;
    ui_draw_string(fb, stride, x, y, "Continue?", UI_COLOR_FG);
    draw_footer(fb, stride, "ENTER confirm  ESC cancel");
}

/* ── media menu ────────────────────────────────────────────────────────── */

static void render_disk_menu(uint8_t *fb, int stride) {
    layout_window(MENU_ROWS * (UI_LINE_H + 4));
    draw_chrome(fb, stride, " Media Browser ");

    int x  = content_x();
    int y  = content_y();
    int cw = content_w();

    const int label_w   = 9 * UI_CHAR_W;
    const int name_avail = cw - 8 - label_w;
    /* Clamped to what a filename can actually be.  On a wide screen the row
     * has room for more characters than FRANK_DISK_FILENAME_LEN allows, and
     * the truncation branch below sizes a stack buffer from this. */
    int max_chars = name_avail / UI_CHAR_W;
    if (max_chars > FRANK_DISK_FILENAME_LEN - 1) max_chars = FRANK_DISK_FILENAME_LEN - 1;

    for (int row = 0; row < MENU_ROWS; ++row) {
        bool    sel = (row == s_menu_row);
        uint8_t bg  = sel ? UI_COLOR_ACCENT    : UI_COLOR_BG;
        uint8_t fg  = sel ? UI_COLOR_ACCENT_FG : UI_COLOR_FG;

        ui_fill_rect(fb, stride, x, y, cw, UI_LINE_H + 2, bg);

        char label[12];
        snprintf(label, sizeof(label), "Drive %d:", row);
        ui_draw_string(fb, stride, x + 4, y + 2, label, fg);

        const char *name = frank_disc_name(row);
        if (name) {
            int len = (int)strlen(name);
            char buf[FRANK_DISK_FILENAME_LEN + 4];
            if (len <= max_chars) {
                memcpy(buf, name, (size_t)len + 1);
            } else {
                int show = max_chars - 3;
                if (show < 1) show = 1;
                memcpy(buf, name, (size_t)show);
                buf[show] = '.'; buf[show+1] = '.'; buf[show+2] = '.'; buf[show+3] = '\0';
                len = max_chars;
            }
            int nx = x + cw - 4 - len * UI_CHAR_W;
            ui_draw_string(fb, stride, nx, y + 2, buf, fg);
        } else {
            const char *empty = "(empty)";
            int nx = x + cw - 4 - (int)strlen(empty) * UI_CHAR_W;
            ui_draw_string(fb, stride, nx, y + 2, empty,
                           sel ? UI_COLOR_ACCENT_FG : UI_COLOR_DIM);
        }
        y += UI_LINE_H + 4;
    }

    draw_footer(fb, stride, "UP/DN  ENTER=select  ESC");
}

/* ── disc browser ──────────────────────────────────────────────────────── */

static void render_disk_browser(uint8_t *fb, int stride) {
    int total = disk_total_rows();
    int visible = total;
    if (visible > DISK_VISIBLE_ROWS) visible = DISK_VISIBLE_ROWS;
    if (visible < 1) visible = 1;
    /* content = dir-path line + visible entries + reserved message line */
    layout_window((UI_LINE_H + 2) + visible * (UI_LINE_H + 1) + UI_LINE_H);

    char title[32];
    snprintf(title, sizeof(title), " Drive %d ", s_disk_drive);
    draw_chrome(fb, stride, title);

    int x  = content_x();
    int cw = content_w();

    int dir_y = s_win_y + UI_HEADER_H + WIN_PAD;
    ui_fill_rect  (fb, stride, x, dir_y, cw, UI_LINE_H, UI_COLOR_BG);
    ui_draw_string(fb, stride, x, dir_y + 1, g_frank_disk_dir, UI_COLOR_DIM);

    int y     = dir_y + UI_LINE_H + 2;

    int last = s_disk_scroll + DISK_VISIBLE_ROWS;
    if (last > total) last = total;

    for (int i = s_disk_scroll; i < last; ++i) {
        bool    sel = (i == s_disk_row);
        uint8_t bg  = sel ? UI_COLOR_ACCENT    : UI_COLOR_BG;
        uint8_t fg  = sel ? UI_COLOR_ACCENT_FG : UI_COLOR_FG;

        ui_fill_rect(fb, stride, x, y, cw, UI_LINE_H, bg);

        int max_item_chars = (cw - 10) / UI_CHAR_W;

        if (disk_has_eject() && i == 0) {
            const char *mounted = frank_disc_name(s_disk_drive);
            char item[FRANK_DISK_FILENAME_LEN + 16];
            snprintf(item, sizeof(item), "[Eject: %s]", mounted ? mounted : "");
            int len = (int)strlen(item);
            if (len > max_item_chars && max_item_chars > 3) {
                item[max_item_chars - 3] = '.';
                item[max_item_chars - 2] = '.';
                item[max_item_chars - 1] = '.';
                item[max_item_chars]     = '\0';
            }
            ui_draw_string(fb, stride, x + 2, y + 1, item, fg);
        } else if (i == disk_dotdot_row()) {
            bool at_root = (strcmp(g_frank_disk_dir, "/micro/disk") == 0
                         || strcmp(g_frank_disk_dir, "/") == 0);
            uint8_t dfg = (!at_root || sel) ? fg : UI_COLOR_DIM;
            ui_draw_string(fb, stride, x + 2, y + 1, "[..]", dfg);
        } else {
            int ei = i - disk_entry_offset();
            char item[FRANK_DISK_FILENAME_LEN + 8];
            if (g_frank_disk_entries[ei].is_dir)
                snprintf(item, sizeof(item), "[%s/]", g_frank_disk_entries[ei].name);
            else
                snprintf(item, sizeof(item), " %s",  g_frank_disk_entries[ei].name);
            int len = (int)strlen(item);
            if (len > max_item_chars && max_item_chars > 3) {
                item[max_item_chars - 3] = '.';
                item[max_item_chars - 2] = '.';
                item[max_item_chars - 1] = '.';
                item[max_item_chars]     = '\0';
            }
            ui_draw_string(fb, stride, x + 2, y + 1, item, fg);
        }

        y += UI_LINE_H + 1;
    }

    if (total > DISK_VISIBLE_ROWS) {
        ui_draw_scrollbar(fb, stride,
                          WIN_X + WIN_W - WIN_PAD - 4,
                          dir_y + UI_LINE_H + 2,
                          DISK_VISIBLE_ROWS * (UI_LINE_H + 1) - 1,
                          total, DISK_VISIBLE_ROWS, s_disk_scroll);
    }

    if (s_disk_msg[0]) {
        int msg_y = s_win_y + s_win_h - UI_LINE_H * 2 - WIN_PAD;
        ui_fill_rect  (fb, stride, x, msg_y, cw, UI_LINE_H, UI_COLOR_BG);
        ui_draw_string(fb, stride, x, msg_y + 1, s_disk_msg, UI_COLOR_ACCENT);
    }

    draw_footer(fb, stride, "UP/DN PG  ENTER=mount  ESC=back");
}

/* ── main render ───────────────────────────────────────────────────────── */

/* Geometry of the toast, shared by the renderer and the occlusion query so
 * the two cannot drift apart. */
#define TOAST_Y 4
#define TOAST_H (UI_LINE_H + 2)
static int toast_w(void)  { return (int)strlen(s_toast) * UI_CHAR_W + 12; }
static int toast_x(int stride) { return (stride - toast_w()) / 2; }

#if defined(HDMI_HSTX) || defined(VIDEO_LCD_SPI)
bool frank_ui_row_span(int y, int stride, int *x0, int *x1) {
    bool any = false;
    int lo = 0, hi = 0;

    if (s_toast_frames > 0 && y >= TOAST_Y && y < TOAST_Y + TOAST_H) {
        lo = toast_x(stride);
        hi = lo + toast_w();
        any = true;
    }
    if (s_state != UI_HIDDEN && y >= s_win_y && y < s_win_y + s_win_h) {
        /* A tall window can reach up under the toast. Yield the union, so the
         * blit does not repaint a strip between the two and leave a gap. */
        if (any) {
            if (WIN_X < lo) lo = WIN_X;
            if (WIN_X + WIN_W > hi) hi = WIN_X + WIN_W;
        } else {
            lo = WIN_X;
            hi = WIN_X + WIN_W;
            any = true;
        }
    }

    if (!any) return false;
    *x0 = lo;
    *x1 = hi;
    return true;
}
#endif

void frank_ui_render(uint8_t *fb, int stride, int height) {
    (void)height;

    /* The toast's lifetime ticks whether or not anything gets redrawn, so
     * this runs before the redraw gate below.  When it reaches zero the row
     * span is released and the blit repaints those rows on the next frame. */
    bool toast_visible = s_toast_frames > 0;
    if (toast_visible) --s_toast_frames;

#ifdef HDMI_HSTX
    if (!s_dirty) return;
    s_dirty = false;
#elif defined(VIDEO_LCD_SPI)
    {
        unsigned bit = (fb == SCREEN[1]) ? 2u : 1u;
        if (!(s_dirty & bit)) return;
        s_dirty &= ~bit;
    }
#endif

    if (toast_visible) {
        int tw = toast_w();
        int tx = toast_x(stride);
        int ty = TOAST_Y;
        ui_fill_rect(fb, stride, tx, ty, tw, TOAST_H, UI_COLOR_BG);
        ui_draw_string(fb, stride, tx + 6, ty + 2, s_toast, UI_COLOR_ACCENT);
    }

    if (s_state == UI_HIDDEN)           return;
    if (s_state == UI_SETTINGS)         render_settings_page(fb, stride);
    if (s_state == UI_SETTINGS_CONFIRM) render_settings_confirm(fb, stride);
    if (s_state == UI_DISK_MENU)        render_disk_menu(fb, stride);
    if (s_state == UI_DISK_BROWSER)     render_disk_browser(fb, stride);
}
