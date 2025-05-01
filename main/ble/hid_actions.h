#pragma once

#include <stdint.h>
#include <stdbool.h>

// Action types
typedef enum {
    ACTION_TYPE_KEYBOARD_KEY,
    ACTION_TYPE_KEYBOARD_COMBO,
    ACTION_TYPE_MOUSE_BUTTON,
    ACTION_TYPE_SYSTEM_CONTROL
} action_type_t;

// Keyboard keys
typedef enum {
    KC_A = 0x04, KC_B, KC_C, KC_D, KC_E, KC_F, KC_G, KC_H, KC_I, KC_J, KC_K, KC_L, KC_M,
    KC_N, KC_O, KC_P, KC_Q, KC_R, KC_S, KC_T, KC_U, KC_V, KC_W, KC_X, KC_Y, KC_Z,
    KC_1 = 0x1E, KC_2, KC_3, KC_4, KC_5, KC_6, KC_7, KC_8, KC_9, KC_0,
    KC_ENTER = 0x28, KC_ESCAPE = 0x29, KC_BACKSPACE = 0x2A, KC_TAB = 0x2B, KC_SPACE = 0x2C,
    KC_MINUS = 0x2D, KC_EQUAL = 0x2E,
    KC_LEFT_BRACKET = 0x2F, KC_RIGHT_BRACKET = 0x30,
    KC_BACKSLASH = 0x31, KC_SEMICOLON = 0x33, KC_QUOTE = 0x34,
    KC_GRAVE = 0x35, KC_COMMA = 0x36, KC_DOT = 0x37, KC_SLASH = 0x38,
    KC_CAPS_LOCK = 0x39,
    KC_F1 = 0x3A, KC_F2, KC_F3, KC_F4, KC_F5, KC_F6, KC_F7, KC_F8, KC_F9, KC_F10, KC_F11, KC_F12,
    KC_PRINT_SCREEN = 0x46, KC_SCROLL_LOCK = 0x47, KC_PAUSE = 0x48,
    KC_INSERT = 0x49, KC_HOME = 0x4A, KC_PAGE_UP = 0x4B,
    KC_DELETE = 0x4C, KC_END = 0x4D, KC_PAGE_DOWN = 0x4E,
    KC_RIGHT = 0x4F, KC_LEFT = 0x50, KC_DOWN = 0x51, KC_UP = 0x52,
    KC_NUM_LOCK = 0x53,
    KC_F13 = 0x68, KC_F14, KC_F15, KC_F16, KC_F17, KC_F18, KC_F19, KC_F20, KC_F21, KC_F22, KC_F23, KC_F24
} keyboard_key_t;

// Mouse buttons
typedef enum {
    KC_MS_BTN1 = 0x01,  // Left click
    KC_MS_BTN2 = 0x02,  // Right click
    KC_MS_BTN3 = 0x04   // Middle click
} mouse_button_t;

// System controls
typedef enum {
    KC_SYSTEM_POWER = 0x01,
    KC_SYSTEM_SLEEP = 0x02,
    KC_SYSTEM_WAKE = 0x03
} system_control_t;

// Consumer controls
typedef enum {
    KC_AUDIO_MUTE = 0x00E2,
    KC_AUDIO_VOL_UP = 0x00E9,
    KC_AUDIO_VOL_DOWN = 0x00EA,
    KC_MEDIA_PLAY_PAUSE = 0x00CD,
    KC_MEDIA_NEXT_TRACK = 0x00B5,
    KC_MEDIA_PREV_TRACK = 0x00B6,
    KC_MEDIA_STOP = 0x00B7,
    KC_MEDIA_EJECT = 0x00B8,
    KC_MEDIA_FAST_FORWARD = 0x00B3,
    KC_MEDIA_REWIND = 0x00B4,
    KC_MEDIA_SELECT = 0x0183,
    KC_MAIL = 0x018A,
    KC_CALCULATOR = 0x0192,
    KC_MY_COMPUTER = 0x0194,
    KC_WWW_SEARCH = 0x0221,
    KC_WWW_HOME = 0x0223,
    KC_WWW_BACK = 0x0224,
    KC_WWW_FORWARD = 0x0225,
    KC_WWW_STOP = 0x0226,
    KC_WWW_REFRESH = 0x0227,
    KC_WWW_FAVORITES = 0x022A,
    KC_BRIGHTNESS_UP = 0x006F,
    KC_BRIGHTNESS_DOWN = 0x0070
} consumer_control_t;

// Special keys
typedef enum {
    KC_CURSOR_BACK = 0xF0,
    KC_CURSOR_FORWARD = 0xF1,
    KC_CURSOR_SWITCH = 0xF2,
    KC_MS_WH_DOWN = 0xF3,
    KC_MS_WH_UP = 0xF4,
    KC_MS_WH_SWITCH = 0xF5
} special_key_t;

// Modifiers
typedef enum {
    MOD_CTRL = 0x01,
    MOD_SHIFT = 0x02,
    MOD_ALT = 0x04,
    MOD_WIN = 0x08
} key_modifier_t;

// Function prototypes
void execute_keyboard_action(uint16_t conn_id, keyboard_key_t key, uint8_t modifiers);
void execute_mouse_button_action(uint16_t conn_id, mouse_button_t button);
void execute_system_control_action(uint16_t conn_id, system_control_t control);
void execute_consumer_control_action(uint16_t conn_id, consumer_control_t control);
void execute_special_action(uint16_t conn_id, special_key_t action);

// String to value conversion functions
keyboard_key_t string_to_keyboard_key(const char* str);
mouse_button_t string_to_mouse_button(const char* str);
system_control_t string_to_system_control(const char* str);
consumer_control_t string_to_consumer_control(const char* str);
special_key_t string_to_special_key(const char* str);
uint8_t string_to_modifiers(const char** modifiers, int count);

// Execute action from string
void execute_action_from_string(uint16_t conn_id, const char* action_type, const char* action,
                              const char** modifiers, int modifier_count);
