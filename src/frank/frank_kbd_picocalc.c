/*
 * frank-micro — BBC Micro for RP2350
 *
 * Copyright (c) 2026 Tim Hawes <me@timhawes.com>
 * https://github.com/rh1tech/frank-micro
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * frank_kbd_picocalc.c — PicoCalc keyboard events to the BBC key matrix.
 *
 * The key map is docs/PICOCALC.md section 7.  The PicoCalc's keyboard reports
 * characters, not key positions, and its firmware applies Shift itself:
 * Shift+3 arrives as '#'.  So this layer works out which physical key each
 * code came from, presses the BBC key that types the same character, and
 * forces BBC SHIFT on or off while that key is held.  Each physical key
 * remembers what its press did, so the release undoes exactly that even when
 * it arrives with a different code (press 3, press Shift, release: '#').
 *
 * The chord and overlay handling mirrors process_xt_event() in
 * frank_keyboard.c.  It is repeated here rather than shared so that no other
 * board's build changes.
 */
#ifdef PLATFORM_PICOCALC

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "allegro5/allegro.h"   /* ALLEGRO_KEY_* */
#include "keyboard.h"           /* key_down(), key_up() */
#include "picocalc_kbd.h"

#include "frank_ui.h"
#include "frank_screenshot.h"   /* g_frank_screenshot_pending */
#include "frank_kbd_picocalc.h"

extern void main_reset(void);
extern void m6502_reset(void);
extern void video_reset(void);
extern void wd1770_reset(void);

/* Codes the keyboard firmware sends that are not plain ASCII.  From
 * keyboard.h in ClockworkPi's picocalc_keyboard, commit fdcfe2c. */
enum {
    PK_BACKSPACE = 0x08, PK_TAB = 0x09, PK_ENTER = 0x0A,
    PK_F1 = 0x81, PK_F2, PK_F3, PK_F4, PK_F5, PK_F6, PK_F7, PK_F8, PK_F9,
    PK_F10 = 0x90,
    PK_ALT = 0xA1, PK_SHL = 0xA2, PK_SHR = 0xA3, PK_CTRL = 0xA5,
    PK_ESC = 0xB1, PK_LEFT = 0xB4, PK_UP = 0xB5, PK_DOWN = 0xB6, PK_RIGHT = 0xB7,
    PK_CAPS = 0xC1,
    PK_BREAK = 0xD0, PK_INSERT = 0xD1, PK_HOME = 0xD2, PK_DEL = 0xD4,
    PK_END = 0xD5, PK_PGUP = 0xD6, PK_PGDN = 0xD7,
};

/* Every physical key, as {unshifted code, shifted code}.  Letters are listed
 * in upper case and also arrive in lower case.  The order follows the
 * firmware's kbd_entries[] then btn_entries[]; only the pairing matters. */
static const uint8_t s_keys[][2] = {
    {PK_F5, PK_F10}, {PK_F4, PK_F9}, {PK_F3, PK_F8}, {PK_F2, PK_F7}, {PK_F1, PK_F6},
    {'`', '~'}, {'3', '#'}, {'2', '@'},
    {PK_BACKSPACE, 0}, {PK_DEL, PK_END}, {PK_CAPS, 0}, {PK_TAB, PK_HOME},
    {PK_ESC, PK_BREAK}, {'4', '$'}, {'E', 0}, {'W', 0},
    {'P', 0}, {'=', '+'}, {'-', '_'}, {'\\', '|'}, {'/', '?'}, {'R', 0}, {'S', 0}, {'1', '!'},
    {PK_ENTER, PK_INSERT}, {'8', '*'}, {'7', '&'}, {'6', '^'}, {'5', '%'},
    {'F', 0}, {'X', 0}, {'Q', 0},
    {'.', '>'}, {'I', 0}, {'U', 0}, {'Y', 0}, {'T', 0}, {'V', 0}, {';', ':'}, {'A', 0},
    {'L', 0}, {'K', 0}, {'J', 0}, {'H', 0}, {'G', 0}, {'C', 0}, {'\'', '"'}, {'Z', 0},
    {'O', 0}, {',', '<'}, {'M', 0}, {'N', 0}, {'B', 0}, {'D', 0}, {' ', 0},
    {PK_ALT, 0}, {PK_CTRL, 0}, {PK_SHL, 0}, {PK_SHR, 0},
    {'0', ')'}, {'9', '('}, {']', '}'}, {'[', '{'},
    {PK_RIGHT, 0}, {PK_UP, PK_PGUP}, {PK_DOWN, PK_PGDN}, {PK_LEFT, 0},
};
#define NKEYS ((int)(sizeof s_keys / sizeof s_keys[0]))

enum { FORCE_NONE = 0, FORCE_OFF, FORCE_ON };

typedef struct {
    bool     down;
    uint8_t  allegro;   /* what the press sent to the BBC, or 0 */
    uint8_t  force;     /* FORCE_* applied to BBC SHIFT while held */
    uint32_t seq;       /* press order, so the newest forcing key wins */
} key_state_t;

static uint8_t     s_code_to_key[256];   /* code -> key index + 1, 0 = unknown */
static key_state_t s_state[NKEYS];
static uint32_t    s_seq;
static int         s_phys_shift;         /* Shift keys physically down */
static bool        s_alt, s_ctrl;
static bool        s_bbc_shift, s_bbc_ctrl;

/* BREAK is a reset, and the MOS reads SHIFT and CTRL a little after it, so
 * the state a BREAK chord asks for is held for a second regardless of how
 * briefly the key was tapped.  frank_disc.c's autoboot does the same. */
#define BREAK_HOLD_FRAMES 50
static int     s_break_frames;
static uint8_t s_break_shift;            /* FORCE_* */
static bool    s_break_ctrl;

void frank_kbd_picocalc_init(void) {
    for (int k = 0; k < NKEYS; k++) {
        for (int j = 0; j < 2; j++) {
            uint8_t c = s_keys[k][j];
            if (!c) continue;
            s_code_to_key[c] = (uint8_t)(k + 1);
            if (c >= 'A' && c <= 'Z') s_code_to_key[c + 32] = (uint8_t)(k + 1);
        }
    }
    picocalc_kbd_init();
}

/* ── BBC SHIFT and CTRL ───────────────────────────────────────────────────── */

static void bbc_update_modifiers(void) {
    bool shift = s_phys_shift > 0;
    uint32_t best = 0;
    for (int k = 0; k < NKEYS; k++) {
        const key_state_t *s = &s_state[k];
        if (s->down && s->force != FORCE_NONE && s->seq > best) {
            best = s->seq;
            shift = (s->force == FORCE_ON);
        }
    }
    if (s_break_frames > 0 && s_break_shift != FORCE_NONE)
        shift = (s_break_shift == FORCE_ON);

    if (shift != s_bbc_shift) {
        s_bbc_shift = shift;
        if (shift) key_down(ALLEGRO_KEY_LSHIFT); else key_up(ALLEGRO_KEY_LSHIFT);
    }

    bool ctrl = s_ctrl || (s_break_frames > 0 && s_break_ctrl);
    if (ctrl != s_bbc_ctrl) {
        s_bbc_ctrl = ctrl;
        if (ctrl) key_down(ALLEGRO_KEY_LCTRL); else key_up(ALLEGRO_KEY_LCTRL);
    }
}

static void bbc_break(uint8_t shift, bool ctrl) {
    s_break_shift = shift;
    s_break_ctrl = ctrl;
    s_break_frames = BREAK_HOLD_FRAMES;
    bbc_update_modifiers();
    /* What B-em does for its own BREAK key (main_key_down(), the F12 case). */
    m6502_reset();
    video_reset();
    wd1770_reset();
}

/* ── Code to BBC key ──────────────────────────────────────────────────────── */

/* The Allegro code that types this character on the BBC, and the SHIFT state
 * it needs.  Table in docs/PICOCALC.md section 7.3.  Returns false for codes
 * that are not ordinary keys. */
static bool bbc_key_for(uint8_t c, uint8_t *allegro, uint8_t *force) {
    *force = FORCE_NONE;
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c >= 'A' && c <= 'Z') { *allegro = (uint8_t)(ALLEGRO_KEY_A + (c - 'A')); return true; }
    if (c >= '0' && c <= '9') { *allegro = (uint8_t)(ALLEGRO_KEY_0 + (c - '0')); return true; }

    static const struct { uint8_t c, allegro, force; } map[] = {
        {'!', ALLEGRO_KEY_1, FORCE_ON},  {'"', ALLEGRO_KEY_2, FORCE_ON},
        {'#', ALLEGRO_KEY_3, FORCE_ON},  {'$', ALLEGRO_KEY_4, FORCE_ON},
        {'%', ALLEGRO_KEY_5, FORCE_ON},  {'&', ALLEGRO_KEY_6, FORCE_ON},
        {'\'', ALLEGRO_KEY_7, FORCE_ON}, {'(', ALLEGRO_KEY_8, FORCE_ON},
        {')', ALLEGRO_KEY_9, FORCE_ON},
        {'@', ALLEGRO_KEY_OPENBRACE, FORCE_OFF},
        {'^', ALLEGRO_KEY_EQUALS, FORCE_OFF},     {'~', ALLEGRO_KEY_EQUALS, FORCE_ON},
        {'-', ALLEGRO_KEY_MINUS, FORCE_OFF},      {'=', ALLEGRO_KEY_MINUS, FORCE_ON},
        {'_', ALLEGRO_KEY_TILDE, FORCE_OFF},      {'`', ALLEGRO_KEY_TILDE, FORCE_ON},
        {'[', ALLEGRO_KEY_CLOSEBRACE, FORCE_OFF}, {'{', ALLEGRO_KEY_CLOSEBRACE, FORCE_ON},
        {']', ALLEGRO_KEY_BACKSLASH, FORCE_OFF},  {'}', ALLEGRO_KEY_BACKSLASH, FORCE_ON},
        {'\\', ALLEGRO_KEY_BACKSLASH2, FORCE_OFF},{'|', ALLEGRO_KEY_BACKSLASH2, FORCE_ON},
        {';', ALLEGRO_KEY_SEMICOLON, FORCE_OFF},  {'+', ALLEGRO_KEY_SEMICOLON, FORCE_ON},
        {':', ALLEGRO_KEY_QUOTE, FORCE_OFF},      {'*', ALLEGRO_KEY_QUOTE, FORCE_ON},
        {',', ALLEGRO_KEY_COMMA, FORCE_OFF},      {'<', ALLEGRO_KEY_COMMA, FORCE_ON},
        {'.', ALLEGRO_KEY_FULLSTOP, FORCE_OFF},   {'>', ALLEGRO_KEY_FULLSTOP, FORCE_ON},
        {'/', ALLEGRO_KEY_SLASH, FORCE_OFF},      {'?', ALLEGRO_KEY_SLASH, FORCE_ON},
        {' ', ALLEGRO_KEY_SPACE, FORCE_NONE},
        /* Positional keys: physical Shift passes through. */
        {PK_ENTER, ALLEGRO_KEY_ENTER, FORCE_NONE},  {PK_INSERT, ALLEGRO_KEY_ENTER, FORCE_NONE},
        {PK_TAB, ALLEGRO_KEY_TAB, FORCE_NONE},      {PK_HOME, ALLEGRO_KEY_TAB, FORCE_NONE},
        {PK_BACKSPACE, ALLEGRO_KEY_BACKSPACE, FORCE_NONE},
        {PK_DEL, ALLEGRO_KEY_DELETE, FORCE_NONE},
        {PK_END, ALLEGRO_KEY_END, FORCE_OFF},       /* COPY */
        {PK_CAPS, ALLEGRO_KEY_CAPSLOCK, FORCE_NONE},
        {PK_ESC, ALLEGRO_KEY_ESCAPE, FORCE_NONE},
        {PK_UP, ALLEGRO_KEY_UP, FORCE_NONE},        {PK_PGUP, ALLEGRO_KEY_UP, FORCE_NONE},
        {PK_DOWN, ALLEGRO_KEY_DOWN, FORCE_NONE},    {PK_PGDN, ALLEGRO_KEY_DOWN, FORCE_NONE},
        {PK_LEFT, ALLEGRO_KEY_LEFT, FORCE_NONE},    {PK_RIGHT, ALLEGRO_KEY_RIGHT, FORCE_NONE},
        /* B-em's ALLEGRO_KEY_F1 is BBC f0, so f1 is F2.  F6-F10 are the
         * Shift layer of F1-F5, so SHIFT is forced off for them. */
        {PK_F1, ALLEGRO_KEY_F2, FORCE_NONE}, {PK_F2, ALLEGRO_KEY_F3, FORCE_NONE},
        {PK_F3, ALLEGRO_KEY_F4, FORCE_NONE}, {PK_F4, ALLEGRO_KEY_F5, FORCE_NONE},
        {PK_F5, ALLEGRO_KEY_F6, FORCE_NONE},
        {PK_F6, ALLEGRO_KEY_F7, FORCE_OFF},  {PK_F7, ALLEGRO_KEY_F8, FORCE_OFF},
        {PK_F8, ALLEGRO_KEY_F9, FORCE_OFF},  {PK_F9, ALLEGRO_KEY_F10, FORCE_OFF},
        {PK_F10, ALLEGRO_KEY_F1, FORCE_OFF},
    };
    for (unsigned i = 0; i < sizeof map / sizeof map[0]; i++) {
        if (map[i].c == c) {
            *allegro = map[i].allegro;
            *force = map[i].force;
            return true;
        }
    }
    return false;
}

/* The overlay's keysym for a code, or 0. */
static unsigned int overlay_ks_for(uint8_t c) {
    switch (c) {
        case PK_ESC:       return FRANK_KS_Escape;
        case PK_ENTER:     return FRANK_KS_Return;
        case PK_BACKSPACE: return FRANK_KS_BackSpace;
        case PK_DEL:       return FRANK_KS_Delete;
        case PK_UP:        return FRANK_KS_Up;
        case PK_DOWN:      return FRANK_KS_Down;
        case PK_LEFT:      return FRANK_KS_Left;
        case PK_RIGHT:     return FRANK_KS_Right;
        case PK_PGUP:      return FRANK_KS_Page_Up;
        case PK_PGDN:      return FRANK_KS_Page_Down;
        default:           return 0;
    }
}

/* ── Events ───────────────────────────────────────────────────────────────── */

static void log_event(uint8_t state, uint8_t code, uint8_t allegro, uint8_t force) {
    printf("KBD st=%u key=0x%02x -> al=%u%s\n", state, code, allegro,
           force == FORCE_ON ? " shift forced on" : force == FORCE_OFF ? " shift forced off" : "");
}

/* Alt chords, on the press.  True if the key was a chord and is consumed. */
static bool handle_alt_chord(int key) {
    uint8_t base = s_keys[key][0];
    if (base == PK_DEL && s_ctrl) { main_reset(); return true; }
    switch (base) {
        case 'D':    frank_ui_open_disk_menu(); return true;
        case 'S':    frank_ui_toggle(); return true;
        case 'P':    g_frank_screenshot_pending = true; return true;
        case PK_ESC: bbc_break(FORCE_ON, false); return true;        /* SHIFT-BREAK */
        default:     return false;
    }
}

static void on_press(int key, uint8_t code) {
    key_state_t *s = &s_state[key];
    if (s->down) return;     /* the firmware's hold re-report */
    uint8_t base = s_keys[key][0];

    /* Modifiers. */
    if (base == PK_SHL || base == PK_SHR) { s->down = true; s_phys_shift++; bbc_update_modifiers(); return; }
    if (base == PK_CTRL) { s->down = true; s_ctrl = true; bbc_update_modifiers(); return; }
    if (base == PK_ALT)  { s->down = true; s_alt = true; return; }

    /* Alt never reaches the BBC.  A press with Alt held is a chord or is
     * dropped, and is never recorded: the firmware turns Alt+I into the code
     * Shift+Enter also sends. */
    if (s_alt) {
        if (base == PK_CAPS) {           /* SHIFT LOCK */
            s->down = true; s->allegro = ALLEGRO_KEY_PAD_EQUALS; s->force = FORCE_NONE;
            s->seq = ++s_seq;
            key_down(ALLEGRO_KEY_PAD_EQUALS);
            log_event(PICOCALC_KBD_PRESSED, code, s->allegro, s->force);
            return;
        }
        handle_alt_chord(key);
        return;
    }

    if (frank_ui_wants_keys()) {
        unsigned int ks = overlay_ks_for(code);
        if (ks) frank_ui_handle_key(ks);
        return;
    }

    /* BREAK in its plain and CTRL forms.  SHIFT-BREAK is the Alt+Esc chord. */
    if (code == PK_BREAK) {
        bbc_break(FORCE_OFF, s_ctrl);
        printf("KBD %sBREAK\n", s_ctrl ? "CTRL-" : "");
        return;
    }

    uint8_t allegro, force;
    if (!bbc_key_for(code, &allegro, &force)) return;
    s->down = true;
    s->allegro = allegro;
    s->force = force;
    s->seq = ++s_seq;
    bbc_update_modifiers();      /* SHIFT before the key it applies to */
    key_down(allegro);
    log_event(PICOCALC_KBD_PRESSED, code, allegro, force);
}

static void on_release(int key, uint8_t code) {
    key_state_t *s = &s_state[key];
    if (!s->down) return;
    s->down = false;
    uint8_t base = s_keys[key][0];

    if (base == PK_SHL || base == PK_SHR) { if (s_phys_shift > 0) s_phys_shift--; bbc_update_modifiers(); return; }
    if (base == PK_CTRL) { s_ctrl = false; bbc_update_modifiers(); return; }
    if (base == PK_ALT)  { s_alt = false; return; }

    if (s->allegro) {
        key_up(s->allegro);
        log_event(PICOCALC_KBD_RELEASED, code, s->allegro, s->force);
    }
    s->allegro = 0;
    s->force = FORCE_NONE;
    bbc_update_modifiers();
}

static void on_event(uint8_t state, uint8_t code) {
    int key = (int)s_code_to_key[code] - 1;
    if (key < 0) return;     /* unknown, or the power button */
    if (state == PICOCALC_KBD_PRESSED)       on_press(key, code);
    else if (state == PICOCALC_KBD_RELEASED) on_release(key, code);
    /* PICOCALC_KBD_HOLD: ignored.  The BBC OS does its own auto-repeat. */
}

void frank_kbd_picocalc_poll(void) {
    picocalc_kbd_poll(on_event);
    if (s_break_frames > 0 && --s_break_frames == 0) bbc_update_modifiers();
}

#endif /* PLATFORM_PICOCALC */
