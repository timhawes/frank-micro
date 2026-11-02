/*
 * B-em Pico Version (C) 2021 Graham Sanderson
 */
#ifndef STUB_ALLEGRO_H
#define STUB_ALLEGRO_H

#if PICO_BUILD
#include "pico.h"
#include "pico/time.h"
#else
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
typedef unsigned int uint;
#endif
#ifdef __cplusplus
extern "C" {
#endif

extern int errno; // hack

#define ALLEGRO_NATIVE_PATH_SEP '/'

typedef struct ALLEGRO_KEYBOARD_STATE {
    uint8_t dummy;
} ALLEGRO_KEYBOARD_STATE;


typedef struct ALLEGRO_SAMPLE ALLEGRO_SAMPLE;
typedef struct ALLEGRO_SAMPLE_ID {
    uint8_t _index; // not used
    uint8_t _id;
} ALLEGRO_SAMPLE_ID;
typedef struct ALLEGRO_TIMER ALLEGRO_TIMER;
typedef struct ALLEGRO_PATH ALLEGRO_PATH;
typedef struct ALLEGRO_CONFIG ALLEGRO_CONFIG;
typedef struct ALLEGRO_EVENT_QUEUE ALLEGRO_EVENT_QUEUE;
typedef struct ALLEGRO_SAMPLE ALLEGRO_SAMPLE;
typedef struct ALLEGRO_VOICE ALLEGRO_VOICE;
typedef struct ALLEGRO_AUDIO_STREAM ALLEGRO_AUDIO_STREAM;
typedef struct ALLEGRO_MIXER ALLEGRO_MIXER;

typedef enum ALLEGRO_AUDIO_DEPTH {
    ALLEGRO_AUDIO_DEPTH_INT16
} ALLEGRO_AUDIO_DEPTH;

typedef enum ALLEGRO_CHANNEL_CONF {
    ALLEGRO_CHANNEL_CONF_1,
    ALLEGRO_CHANNEL_CONF_2
} ALLEGRO_CHANNEL_CONF;

typedef enum ALLEGRO_PLAYMODE {
    ALLEGRO_PLAYMODE_ONCE,
    ALLEGRO_PLAYMODE_LOOP
} ALLEGRO_PLAYMODE;

typedef struct ALLEGRO_DISPLAY ALLEGRO_DISPLAY;
typedef struct ALLEGRO_EVENT_SOURCE {
    uint8_t dummy;
} ALLEGRO_EVENT_SOURCE;
typedef struct ALLEGRO_USTR ALLEGRO_USTR;
typedef struct ALLEGRO_CONFIG_SECTION ALLEGRO_CONFIG_SECTION;
typedef struct ALLEGRO_JOYSTICK ALLEGRO_JOYSTICK;
typedef struct ALLEGRO_USER_EVENT ALLEGRO_USER_EVENT;
typedef struct ALLEGRO_BITMAP ALLEGRO_BITMAP;
typedef struct ALLEGRO_LOCKED_REGION ALLEGRO_LOCKED_REGION;
typedef struct ALLEGRO_COLOR ALLEGRO_COLOR;

#define ALLEGRO_EVENT_HEADER \
    uint type; \
    ALLEGRO_EVENT_SOURCE *source; \
    double timestamp; // todo yuk

typedef struct ALLEGRO_ANY_EVENT {
    ALLEGRO_EVENT_HEADER;
} ALLEGRO_ANY_EVENT;

typedef struct ALLEGRO_KEYBOARD_EVENT {
    ALLEGRO_EVENT_HEADER;
    int keycode;
} ALLEGRO_KEYBOARD_EVENT;

typedef struct ALLEGRO_MOUSE_EVENT {
    ALLEGRO_EVENT_HEADER;
    int dx, dy;
    int button;
} ALLEGRO_MOUSE_EVENT;

typedef struct ALLEGRO_JOYSTICK_EVENT {
    ALLEGRO_EVENT_HEADER;
    ALLEGRO_JOYSTICK *id;
    int stick;
    int axis;
    int pos;
    int button;
} ALLEGRO_JOYSTICK_EVENT;

typedef struct ALLEGRO_EVENT {
    union {
        struct {
            ALLEGRO_EVENT_HEADER
        };
        ALLEGRO_ANY_EVENT any;
        ALLEGRO_KEYBOARD_EVENT keyboard;
        ALLEGRO_MOUSE_EVENT mouse;
        ALLEGRO_JOYSTICK_EVENT joystick;
    };
} ALLEGRO_EVENT;

bool al_install_system(int version, int (*atexit_ptr)(void (*)(void)));
ALLEGRO_EVENT_QUEUE *al_create_event_queue(void);
ALLEGRO_EVENT_SOURCE *al_get_display_event_source(ALLEGRO_DISPLAY *display);
ALLEGRO_PATH *al_get_standard_path(int id);
ALLEGRO_PATH *al_create_path_for_directory(const char *str);
ALLEGRO_PATH *al_create_path(const char *str);
void al_append_path_component(ALLEGRO_PATH *path, const char *s);
void al_set_path_filename(ALLEGRO_PATH *path, const char *filename);
bool al_set_path_extension(ALLEGRO_PATH *path, char const *extension);
bool al_join_paths(ALLEGRO_PATH *path, const ALLEGRO_PATH *tail);
const char *al_get_path_filename(const ALLEGRO_PATH *path);
const char *al_path_cstr(const ALLEGRO_PATH *path, char delim);
const char *al_get_path_extension(const ALLEGRO_PATH *path);
void al_destroy_path(ALLEGRO_PATH *path);
ALLEGRO_CONFIG *al_create_config(void);
ALLEGRO_CONFIG *al_load_config_file(const char *filename);
bool al_save_config_file(const char *filename, const ALLEGRO_CONFIG *config);
char const *al_get_first_config_section(ALLEGRO_CONFIG const *config, ALLEGRO_CONFIG_SECTION **iterator);
char const *al_get_next_config_section(ALLEGRO_CONFIG_SECTION **iterator);
const char *al_get_config_value(const ALLEGRO_CONFIG *config, const char *section, const char *key);
void al_set_config_value(ALLEGRO_CONFIG *config, const char *section, const char *key, const char *value);
bool al_remove_config_key(ALLEGRO_CONFIG *config, char const *section, char const *key);
void al_destroy_config(ALLEGRO_CONFIG *config);
void al_register_event_source(ALLEGRO_EVENT_QUEUE *, ALLEGRO_EVENT_SOURCE *);
void al_init_user_event_source(ALLEGRO_EVENT_SOURCE *);
ALLEGRO_EVENT_SOURCE *al_get_keyboard_event_source(void);
bool al_install_joystick();
int al_get_num_joysticks(void);
ALLEGRO_JOYSTICK *al_get_joystick(int joyn);
const char *al_get_joystick_name(ALLEGRO_JOYSTICK *joystick);
int al_get_joystick_num_sticks(ALLEGRO_JOYSTICK *joystick);
int al_get_joystick_num_buttons(ALLEGRO_JOYSTICK *joystick);
int al_get_joystick_num_axes(ALLEGRO_JOYSTICK *joystick, int stick);
ALLEGRO_EVENT_SOURCE *al_get_joystick_event_source(void);
bool al_install_mouse(void);
ALLEGRO_EVENT_SOURCE *al_get_mouse_event_source(void);
void al_start_timer(ALLEGRO_TIMER *timer);
void al_set_timer_speed(ALLEGRO_TIMER *timer, double speed_secs);
void al_stop_timer(ALLEGRO_TIMER *timer);
ALLEGRO_TIMER *al_create_timer(double speed_secs);
ALLEGRO_EVENT_SOURCE *al_get_timer_event_source(ALLEGRO_TIMER *timer);


bool al_install_keyboard(void);
void al_get_keyboard_state(ALLEGRO_KEYBOARD_STATE *ret_state);
bool al_key_down(const ALLEGRO_KEYBOARD_STATE *, int keycode);


double al_get_time(void);
void al_wait_for_event(ALLEGRO_EVENT_QUEUE *,
                       ALLEGRO_EVENT *ret_event);

bool al_emit_user_event(ALLEGRO_EVENT_SOURCE *, ALLEGRO_EVENT *,
                        void (*dtor)(ALLEGRO_USER_EVENT *));
size_t al_ustr_append_chr(ALLEGRO_USTR *us, int32_t c);

bool al_install_audio(void);
bool al_reserve_samples(int reserve_samples);
bool al_init_acodec_addon(void);
ALLEGRO_SAMPLE *al_load_sample(const char *filename);
unsigned int al_get_sample_frequency(const ALLEGRO_SAMPLE *spl);
unsigned int al_get_sample_length(const ALLEGRO_SAMPLE *spl);
bool al_play_sample(ALLEGRO_SAMPLE *data,
                    float gain, float pan, float speed, ALLEGRO_PLAYMODE loop, ALLEGRO_SAMPLE_ID *ret_id);
void al_stop_sample(ALLEGRO_SAMPLE_ID *spl_id);
void al_destroy_sample(ALLEGRO_SAMPLE *spl);
bool al_attach_audio_stream_to_mixer(ALLEGRO_AUDIO_STREAM *stream,
                                     ALLEGRO_MIXER *mixer);
bool al_attach_mixer_to_voice(ALLEGRO_MIXER *mixer,
                              ALLEGRO_VOICE *voice);
ALLEGRO_VOICE *al_create_voice(unsigned int freq,
                               ALLEGRO_AUDIO_DEPTH depth,
                               ALLEGRO_CHANNEL_CONF chan_conf);
void al_destroy_voice(ALLEGRO_VOICE *voice);
ALLEGRO_AUDIO_STREAM *al_create_audio_stream(size_t buffer_count,
                                             unsigned int samples, unsigned int freq,
                                             ALLEGRO_AUDIO_DEPTH depth, ALLEGRO_CHANNEL_CONF chan_conf);
void al_destroy_audio_stream(ALLEGRO_AUDIO_STREAM *stream);
ALLEGRO_MIXER *al_create_mixer(unsigned int freq,
                               ALLEGRO_AUDIO_DEPTH depth, ALLEGRO_CHANNEL_CONF chan_conf);
void al_destroy_mixer(ALLEGRO_MIXER *mixer);
ALLEGRO_EVENT_SOURCE *al_get_audio_stream_event_source(ALLEGRO_AUDIO_STREAM *stream);
void *al_get_audio_stream_fragment(const ALLEGRO_AUDIO_STREAM *stream);
bool al_set_audio_stream_fragment(ALLEGRO_AUDIO_STREAM *stream, void *val);
bool al_set_audio_stream_playing(ALLEGRO_AUDIO_STREAM *stream, bool val);

enum {
    ALLEGRO_KEY_A = 1,
    ALLEGRO_KEY_B = 2,
    ALLEGRO_KEY_C = 3,
    ALLEGRO_KEY_D = 4,
    ALLEGRO_KEY_E = 5,
    ALLEGRO_KEY_F = 6,
    ALLEGRO_KEY_G = 7,
    ALLEGRO_KEY_H = 8,
    ALLEGRO_KEY_I = 9,
    ALLEGRO_KEY_J = 10,
    ALLEGRO_KEY_K = 11,
    ALLEGRO_KEY_L = 12,
    ALLEGRO_KEY_M = 13,
    ALLEGRO_KEY_N = 14,
    ALLEGRO_KEY_O = 15,
    ALLEGRO_KEY_P = 16,
    ALLEGRO_KEY_Q = 17,
    ALLEGRO_KEY_R = 18,
    ALLEGRO_KEY_S = 19,
    ALLEGRO_KEY_T = 20,
    ALLEGRO_KEY_U = 21,
    ALLEGRO_KEY_V = 22,
    ALLEGRO_KEY_W = 23,
    ALLEGRO_KEY_X = 24,
    ALLEGRO_KEY_Y = 25,
    ALLEGRO_KEY_Z = 26,

    ALLEGRO_KEY_0 = 27,
    ALLEGRO_KEY_1 = 28,
    ALLEGRO_KEY_2 = 29,
    ALLEGRO_KEY_3 = 30,
    ALLEGRO_KEY_4 = 31,
    ALLEGRO_KEY_5 = 32,
    ALLEGRO_KEY_6 = 33,
    ALLEGRO_KEY_7 = 34,
    ALLEGRO_KEY_8 = 35,
    ALLEGRO_KEY_9 = 36,

    ALLEGRO_KEY_PAD_0 = 37,
    ALLEGRO_KEY_PAD_1 = 38,
    ALLEGRO_KEY_PAD_2 = 39,
    ALLEGRO_KEY_PAD_3 = 40,
    ALLEGRO_KEY_PAD_4 = 41,
    ALLEGRO_KEY_PAD_5 = 42,
    ALLEGRO_KEY_PAD_6 = 43,
    ALLEGRO_KEY_PAD_7 = 44,
    ALLEGRO_KEY_PAD_8 = 45,
    ALLEGRO_KEY_PAD_9 = 46,

    ALLEGRO_KEY_F1 = 47,
    ALLEGRO_KEY_F2 = 48,
    ALLEGRO_KEY_F3 = 49,
    ALLEGRO_KEY_F4 = 50,
    ALLEGRO_KEY_F5 = 51,
    ALLEGRO_KEY_F6 = 52,
    ALLEGRO_KEY_F7 = 53,
    ALLEGRO_KEY_F8 = 54,
    ALLEGRO_KEY_F9 = 55,
    ALLEGRO_KEY_F10 = 56,
    ALLEGRO_KEY_F11 = 57,
    ALLEGRO_KEY_F12 = 58,

    ALLEGRO_KEY_ESCAPE = 59,
    ALLEGRO_KEY_TILDE = 60,
    ALLEGRO_KEY_MINUS = 61,
    ALLEGRO_KEY_EQUALS = 62,
    ALLEGRO_KEY_BACKSPACE = 63,
    ALLEGRO_KEY_TAB = 64,
    ALLEGRO_KEY_OPENBRACE = 65,
    ALLEGRO_KEY_CLOSEBRACE = 66,
    ALLEGRO_KEY_ENTER = 67,
    ALLEGRO_KEY_SEMICOLON = 68,
    ALLEGRO_KEY_QUOTE = 69,
    ALLEGRO_KEY_BACKSLASH = 70,
    ALLEGRO_KEY_BACKSLASH2 = 71, /* DirectInput calls this DIK_OEM_102: "< > | on UK/Germany keyboards" */
    ALLEGRO_KEY_COMMA = 72,
    ALLEGRO_KEY_FULLSTOP = 73,
    ALLEGRO_KEY_SLASH = 74,
    ALLEGRO_KEY_SPACE = 75,

    ALLEGRO_KEY_INSERT = 76,
    ALLEGRO_KEY_DELETE = 77,
    ALLEGRO_KEY_HOME = 78,
    ALLEGRO_KEY_END = 79,
    ALLEGRO_KEY_PGUP = 80,
    ALLEGRO_KEY_PGDN = 81,
    ALLEGRO_KEY_LEFT = 82,
    ALLEGRO_KEY_RIGHT = 83,
    ALLEGRO_KEY_UP = 84,
    ALLEGRO_KEY_DOWN = 85,

    ALLEGRO_KEY_PAD_SLASH = 86,
    ALLEGRO_KEY_PAD_ASTERISK = 87,
    ALLEGRO_KEY_PAD_MINUS = 88,
    ALLEGRO_KEY_PAD_PLUS = 89,
    ALLEGRO_KEY_PAD_DELETE = 90,
    ALLEGRO_KEY_PAD_ENTER = 91,

    ALLEGRO_KEY_PRINTSCREEN = 92,
    ALLEGRO_KEY_PAUSE = 93,

    ALLEGRO_KEY_ABNT_C1 = 94,
    ALLEGRO_KEY_YEN = 95,
    ALLEGRO_KEY_KANA = 96,
    ALLEGRO_KEY_CONVERT = 97,
    ALLEGRO_KEY_NOCONVERT = 98,
    ALLEGRO_KEY_AT = 99,
    ALLEGRO_KEY_CIRCUMFLEX = 100,
    ALLEGRO_KEY_COLON2 = 101,
    ALLEGRO_KEY_KANJI = 102,

    ALLEGRO_KEY_PAD_EQUALS = 103,    /* MacOS X */
    ALLEGRO_KEY_BACKQUOTE = 104,    /* MacOS X */
    ALLEGRO_KEY_SEMICOLON2 = 105,    /* MacOS X -- TODO: ask lillo what this should be */
    ALLEGRO_KEY_COMMAND = 106,    /* MacOS X */

    ALLEGRO_KEY_BACK = 107,        /* Android back key */
    ALLEGRO_KEY_VOLUME_UP = 108,
    ALLEGRO_KEY_VOLUME_DOWN = 109,

    /* Android game keys */
    ALLEGRO_KEY_SEARCH = 110,
    ALLEGRO_KEY_DPAD_CENTER = 111,
    ALLEGRO_KEY_BUTTON_X = 112,
    ALLEGRO_KEY_BUTTON_Y = 113,
    ALLEGRO_KEY_DPAD_UP = 114,
    ALLEGRO_KEY_DPAD_DOWN = 115,
    ALLEGRO_KEY_DPAD_LEFT = 116,
    ALLEGRO_KEY_DPAD_RIGHT = 117,
    ALLEGRO_KEY_SELECT = 118,
    ALLEGRO_KEY_START = 119,
    ALLEGRO_KEY_BUTTON_L1 = 120,
    ALLEGRO_KEY_BUTTON_R1 = 121,
    ALLEGRO_KEY_BUTTON_L2 = 122,
    ALLEGRO_KEY_BUTTON_R2 = 123,
    ALLEGRO_KEY_BUTTON_A = 124,
    ALLEGRO_KEY_BUTTON_B = 125,
    ALLEGRO_KEY_THUMBL = 126,
    ALLEGRO_KEY_THUMBR = 127,

    ALLEGRO_KEY_UNKNOWN = 128,

    /* All codes up to before ALLEGRO_KEY_MODIFIERS can be freely
     * assignedas additional unknown keys, like various multimedia
     * and application keys keyboards may have.
     */

    ALLEGRO_KEY_MODIFIERS = 215,

    ALLEGRO_KEY_LSHIFT = 215,
    ALLEGRO_KEY_RSHIFT = 216,
    ALLEGRO_KEY_LCTRL = 217,
    ALLEGRO_KEY_RCTRL = 218,
    ALLEGRO_KEY_ALT = 219,
    ALLEGRO_KEY_ALTGR = 220,
    ALLEGRO_KEY_LWIN = 221,
    ALLEGRO_KEY_RWIN = 222,
    ALLEGRO_KEY_MENU = 223,
    ALLEGRO_KEY_SCROLLLOCK = 224,
    ALLEGRO_KEY_NUMLOCK = 225,
    ALLEGRO_KEY_CAPSLOCK = 226,

    ALLEGRO_KEY_MAX
};

typedef unsigned int ALLEGRO_EVENT_TYPE;

enum {
    ALLEGRO_EVENT_JOYSTICK_AXIS = 1,
    ALLEGRO_EVENT_JOYSTICK_BUTTON_DOWN = 2,
    ALLEGRO_EVENT_JOYSTICK_BUTTON_UP = 3,
    ALLEGRO_EVENT_JOYSTICK_CONFIGURATION = 4,

    ALLEGRO_EVENT_KEY_DOWN = 10,
    ALLEGRO_EVENT_KEY_CHAR = 11,
    ALLEGRO_EVENT_KEY_UP = 12,

    ALLEGRO_EVENT_MOUSE_AXES = 20,
    ALLEGRO_EVENT_MOUSE_BUTTON_DOWN = 21,
    ALLEGRO_EVENT_MOUSE_BUTTON_UP = 22,
    ALLEGRO_EVENT_MOUSE_ENTER_DISPLAY = 23,
    ALLEGRO_EVENT_MOUSE_LEAVE_DISPLAY = 24,
    ALLEGRO_EVENT_MOUSE_WARPED = 25,

    ALLEGRO_EVENT_TIMER = 30,

    ALLEGRO_EVENT_DISPLAY_EXPOSE = 40,
    ALLEGRO_EVENT_DISPLAY_RESIZE = 41,
    ALLEGRO_EVENT_DISPLAY_CLOSE = 42,
    ALLEGRO_EVENT_DISPLAY_LOST = 43,
    ALLEGRO_EVENT_DISPLAY_FOUND = 44,
    ALLEGRO_EVENT_DISPLAY_SWITCH_IN = 45,
    ALLEGRO_EVENT_DISPLAY_SWITCH_OUT = 46,
    ALLEGRO_EVENT_DISPLAY_ORIENTATION = 47,
    ALLEGRO_EVENT_DISPLAY_HALT_DRAWING = 48,
    ALLEGRO_EVENT_DISPLAY_RESUME_DRAWING = 49,

    ALLEGRO_EVENT_TOUCH_BEGIN = 50,
    ALLEGRO_EVENT_TOUCH_END = 51,
    ALLEGRO_EVENT_TOUCH_MOVE = 52,
    ALLEGRO_EVENT_TOUCH_CANCEL = 53,

    ALLEGRO_EVENT_DISPLAY_CONNECTED = 60,
    ALLEGRO_EVENT_DISPLAY_DISCONNECTED = 61,

    ALLEGRO_EVENT_AUDIO_STREAM_FRAGMENT = 513,
    ALLEGRO_EVENT_AUDIO_STREAM_FINISHED = 514,
};

#define al_init()    (al_install_system(0, NULL))

/* frank: allegro memory wrappers map straight to libc. */
#include <stdlib.h>
#ifndef al_malloc
#define al_malloc(n)      malloc(n)
#define al_free(p)        free(p)
#define al_calloc(c, n)   calloc((c), (n))
#define al_realloc(p, n)  realloc((p), (n))
#endif

#if !PICO_NO_HARDWARE
typedef	int64_t	time_t;
static inline time_t time(time_t *tsec) {
    time_t t = to_us_since_boot(get_absolute_time()) / 1000000;   /* seconds since boot */
    if (tsec) *tsec = t;
    return t;
}
#endif
#ifdef __cplusplus
}
#endif
#endif //STUB_ALLEGRO_H
