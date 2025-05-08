#include "hid_actions.h"
#include "esp_hidd_prf_api.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "HID_ACTIONS";

typedef struct {
    char action_type[20];
    char action[20];
    union {
        keyboard_key_t key;
        mouse_button_t button;
        system_control_t system;
        consumer_control_t control;
        special_key_t special;
    } parsed;
    uint8_t type;  // 1=keyboard, 2=mouse, 3=system, 4=consumer, 5=special
} cache_entry_t;

typedef struct {
    uint16_t conn_id;
    uint8_t type;
    union {
        struct { keyboard_key_t key; uint8_t modifiers; };
        mouse_button_t button;
        system_control_t system;
        consumer_control_t consumer;
        special_key_t special;
    } data;
    esp_timer_handle_t timer;
} release_timer_t;

#define CACHE_SIZE 10
static cache_entry_t cache[CACHE_SIZE];
static int cache_count = 0;

static struct {
    bool cursor_y_axis;  // false = X axis, true = Y axis
    bool wheel_horizontal;  // false = vertical, true = horizontal
} state = {
    .cursor_y_axis = false,
    .wheel_horizontal = false
};

static void release_timer_callback(void* arg) {
    release_timer_t* timer_data = (release_timer_t*)arg;
    
    switch (timer_data->type) {
        case 1: // keyboard
            esp_hidd_send_keyboard_value(timer_data->conn_id, 0, (uint8_t[]){0,0,0,0,0,0,0,0});
            break;
        case 2: // mouse
            esp_hidd_send_mouse_value(timer_data->conn_id, 0, 0, 0, 0, 0);
            break;
        case 3: // system
            esp_hidd_send_system_control_value(timer_data->conn_id, 0);
            break;
        case 4: // consumer
            esp_hidd_send_consumer_value(timer_data->conn_id, 0);
            break;
    }
    
    esp_timer_delete(timer_data->timer);
    free(timer_data);
}

static void schedule_release(const uint16_t conn_id, const uint8_t type, const void* data) {
    release_timer_t* timer_data = malloc(sizeof(release_timer_t));
    if (!timer_data) return;

    timer_data->conn_id = conn_id;
    timer_data->type = type;
    memcpy(&timer_data->data, data, sizeof(timer_data->data));

    const esp_timer_create_args_t timer_args = {
        .callback = release_timer_callback,
        .arg = timer_data,
        .name = "release_timer"
    };
    
    if (esp_timer_create(&timer_args, &timer_data->timer) != ESP_OK) {
        free(timer_data);
        return;
    }
    esp_timer_start_once(timer_data->timer, 50000); // 50ms in microseconds
}

void execute_keyboard_action(const uint16_t conn_id, const keyboard_key_t key, const uint8_t modifiers) {
    uint8_t keyboard_cmd[8] = {0};  // 6 keys + 2 reserved
    keyboard_cmd[0] = key;
    esp_hidd_send_keyboard_value(conn_id, modifiers, keyboard_cmd);

    const struct { keyboard_key_t key; uint8_t modifiers; } data = { key, modifiers };
    schedule_release(conn_id, 1, &data);
}

void execute_mouse_button_action(const uint16_t conn_id, const mouse_button_t button) {
    esp_hidd_send_mouse_value(conn_id, button, 0, 0, 0, 0);
    schedule_release(conn_id, 2, &button);
}

void execute_system_control_action(const uint16_t conn_id, const system_control_t control) {
    esp_hidd_send_system_control_value(conn_id, control);
    schedule_release(conn_id, 3, &control);
}

void execute_consumer_control_action(const uint16_t conn_id, const consumer_control_t control) {
    esp_hidd_send_consumer_value(conn_id, control);
    schedule_release(conn_id, 4, &control);
}

void execute_special_action(const uint16_t conn_id, const special_key_t action) {
    switch (action) {
        case KC_CURSOR_BACK:
            if (state.cursor_y_axis) {
                esp_hidd_send_mouse_value(conn_id, 0, 0, -1, 0, 0);
            } else {
                esp_hidd_send_mouse_value(conn_id, 0, -1, 0, 0, 0);
            }
            break;
        case KC_CURSOR_FORWARD:
            if (state.cursor_y_axis) {
                esp_hidd_send_mouse_value(conn_id, 0, 0, 1, 0, 0);
            } else {
                esp_hidd_send_mouse_value(conn_id, 0, 1, 0, 0, 0);
            }
            break;
        case KC_CURSOR_SWITCH:
            state.cursor_y_axis = !state.cursor_y_axis;
            ESP_LOGI(TAG, "Cursor axis switched to %s", state.cursor_y_axis ? "Y" : "X");
            break;
        case KC_MS_WH_DOWN:
            if (state.wheel_horizontal) {
                esp_hidd_send_mouse_value(conn_id, 0, 0, 0, 0, -1);
            } else {
                esp_hidd_send_mouse_value(conn_id, 0, 0, 0, -1, 0);
            }
            break;
        case KC_MS_WH_UP:
            if (state.wheel_horizontal) {
                esp_hidd_send_mouse_value(conn_id, 0, 0, 0, 0, 1);
            } else {
                esp_hidd_send_mouse_value(conn_id, 0, 0, 0, 1, 0);
            }
            break;
        case KC_MS_WH_SWITCH:
            state.wheel_horizontal = !state.wheel_horizontal;
            ESP_LOGI(TAG, "Wheel axis switched to %s", state.wheel_horizontal ? "horizontal" : "vertical");
            break;
        default:
            ESP_LOGW(TAG, "Unknown special action: %d", action);
            break;
    }
}

keyboard_key_t string_to_keyboard_key(const char* str) {
    if (strncmp(str, "KC_", 3) != 0) return 0;
    str += 3;  // Skip "KC_" prefix

    // Single letter keys
    if (strlen(str) == 1 && str[0] >= 'A' && str[0] <= 'Z') {
        return KC_A + (str[0] - 'A');
    }

    // Numbers
    if (strlen(str) == 1 && str[0] >= '0' && str[0] <= '9') {
        return str[0] == '0' ? KC_0 : (KC_1 + (str[0] - '1'));
    }

    // Function keys
    if (str[0] == 'F') {
        const int num = atoi(str + 1);
        if (num >= 1 && num <= 12) return KC_F1 + (num - 1);
        if (num >= 13 && num <= 24) return KC_F13 + (num - 13);
    }

    // Special keys
    const struct {
        const char* name;
        keyboard_key_t key;
    } special_keys[] = {
        {"ENTER", KC_ENTER},
        {"ESCAPE", KC_ESCAPE},
        {"BACKSPACE", KC_BACKSPACE},
        {"TAB", KC_TAB},
        {"SPACE", KC_SPACE},
        {"MINUS", KC_MINUS},
        {"EQUAL", KC_EQUAL},
        {"LEFT_BRACKET", KC_LEFT_BRACKET},
        {"RIGHT_BRACKET", KC_RIGHT_BRACKET},
        {"BACKSLASH", KC_BACKSLASH},
        {"SEMICOLON", KC_SEMICOLON},
        {"QUOTE", KC_QUOTE},
        {"GRAVE", KC_GRAVE},
        {"COMMA", KC_COMMA},
        {"DOT", KC_DOT},
        {"SLASH", KC_SLASH},
        {"CAPS_LOCK", KC_CAPS_LOCK},
        {"PRINT_SCREEN", KC_PRINT_SCREEN},
        {"SCROLL_LOCK", KC_SCROLL_LOCK},
        {"PAUSE", KC_PAUSE},
        {"INSERT", KC_INSERT},
        {"HOME", KC_HOME},
        {"PAGE_UP", KC_PAGE_UP},
        {"DELETE", KC_DELETE},
        {"END", KC_END},
        {"PAGE_DOWN", KC_PAGE_DOWN},
        {"RIGHT", KC_RIGHT},
        {"LEFT", KC_LEFT},
        {"DOWN", KC_DOWN},
        {"UP", KC_UP},
        {"NUM_LOCK", KC_NUM_LOCK},
        {NULL, 0}
    };

    for (int i = 0; special_keys[i].name != NULL; i++) {
        if (strcmp(str, special_keys[i].name) == 0) {
            return special_keys[i].key;
        }
    }

    return 0;
}

mouse_button_t string_to_mouse_button(const char* str) {
    if (strcmp(str, "KC_MS_BTN1") == 0) return KC_MS_BTN1;
    if (strcmp(str, "KC_MS_BTN2") == 0) return KC_MS_BTN2;
    if (strcmp(str, "KC_MS_BTN3") == 0) return KC_MS_BTN3;
    if (strcmp(str, "KC_MS_BTN4") == 0) return KC_MS_BTN4;
    if (strcmp(str, "KC_MS_BTN5") == 0) return KC_MS_BTN5;
    if (strcmp(str, "KC_MS_BTN6") == 0) return KC_MS_BTN6;
    if (strcmp(str, "KC_MS_BTN7") == 0) return KC_MS_BTN7;
    if (strcmp(str, "KC_MS_BTN8") == 0) return KC_MS_BTN8;
    return 0;
}

system_control_t string_to_system_control(const char* str) {
    if (strcmp(str, "KC_SYSTEM_POWER") == 0) return KC_SYSTEM_POWER;
    if (strcmp(str, "KC_SYSTEM_SLEEP") == 0) return KC_SYSTEM_SLEEP;
    if (strcmp(str, "KC_SYSTEM_WAKE") == 0) return KC_SYSTEM_WAKE;
    return 0;
}

consumer_control_t string_to_consumer_control(const char* str) {
    const struct {
        const char* name;
        consumer_control_t code;
    } controls[] = {
        {"KC_AUDIO_MUTE", KC_AUDIO_MUTE},
        {"KC_AUDIO_VOL_UP", KC_AUDIO_VOL_UP},
        {"KC_AUDIO_VOL_DOWN", KC_AUDIO_VOL_DOWN},
        {"KC_MEDIA_PLAY_PAUSE", KC_MEDIA_PLAY_PAUSE},
        {"KC_MEDIA_NEXT_TRACK", KC_MEDIA_NEXT_TRACK},
        {"KC_MEDIA_PREV_TRACK", KC_MEDIA_PREV_TRACK},
        {"KC_MEDIA_STOP", KC_MEDIA_STOP},
        {"KC_MEDIA_EJECT", KC_MEDIA_EJECT},
        {"KC_MEDIA_FAST_FORWARD", KC_MEDIA_FAST_FORWARD},
        {"KC_MEDIA_REWIND", KC_MEDIA_REWIND},
        {"KC_MEDIA_SELECT", KC_MEDIA_SELECT},
        {"KC_MAIL", KC_MAIL},
        {"KC_CALCULATOR", KC_CALCULATOR},
        {"KC_MY_COMPUTER", KC_MY_COMPUTER},
        {"KC_WWW_SEARCH", KC_WWW_SEARCH},
        {"KC_WWW_HOME", KC_WWW_HOME},
        {"KC_MS_BTN4", KC_MS_BTN4},
        {"KC_MS_BTN5", KC_MS_BTN5},
        {"KC_WWW_STOP", KC_WWW_STOP},
        {"KC_WWW_REFRESH", KC_WWW_REFRESH},
        {"KC_WWW_FAVORITES", KC_WWW_FAVORITES},
        {"KC_BRIGHTNESS_UP", KC_BRIGHTNESS_UP},
        {"KC_BRIGHTNESS_DOWN", KC_BRIGHTNESS_DOWN},
        {NULL, 0}
    };

    for (int i = 0; controls[i].name != NULL; i++) {
        if (strcmp(str, controls[i].name) == 0) {
            return controls[i].code;
        }
    }
    return 0;
}

special_key_t string_to_special_key(const char* str) {
    if (strcmp(str, "KC_CURSOR_BACK") == 0) return KC_CURSOR_BACK;
    if (strcmp(str, "KC_CURSOR_FORWARD") == 0) return KC_CURSOR_FORWARD;
    if (strcmp(str, "KC_CURSOR_SWITCH") == 0) return KC_CURSOR_SWITCH;
    if (strcmp(str, "KC_MS_WH_DOWN") == 0) return KC_MS_WH_DOWN;
    if (strcmp(str, "KC_MS_WH_UP") == 0) return KC_MS_WH_UP;
    if (strcmp(str, "KC_MS_WH_SWITCH") == 0) return KC_MS_WH_SWITCH;
    return 0;
}

uint8_t string_to_modifiers(const char** modifiers, const int count) {
    uint8_t mod = 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(modifiers[i], "Ctrl") == 0) mod |= MOD_CTRL;
        else if (strcmp(modifiers[i], "Shift") == 0) mod |= MOD_SHIFT;
        else if (strcmp(modifiers[i], "Alt") == 0) mod |= MOD_ALT;
        else if (strcmp(modifiers[i], "Win") == 0) mod |= MOD_WIN;
    }
    return mod;
}

static IRAM_ATTR cache_entry_t* find_in_cache(const char* action_type, const char* action) {
    for (int i = 0; i < cache_count; i++) {
        if (strcmp(cache[i].action_type, action_type) == 0 && 
            strcmp(cache[i].action, action) == 0) {
            return &cache[i];
        }
    }
    return NULL;
}

static void add_to_cache(const char* action_type, const char* action, const uint8_t type, const void* parsed) {
    if (cache_count >= CACHE_SIZE) {
        memmove(&cache[0], &cache[1], sizeof(cache_entry_t) * (CACHE_SIZE - 1));
        cache_count--;
    }

    cache_entry_t* entry = &cache[cache_count++];
    strncpy(entry->action_type, action_type, sizeof(entry->action_type) - 1);
    strncpy(entry->action, action, sizeof(entry->action) - 1);
    entry->type = type;
    memcpy(&entry->parsed, parsed, sizeof(entry->parsed));
}

void execute_action_from_string(const uint16_t conn_id, const char* action_type, const char* action,
                              const char** modifiers, const int modifier_count) {
    const char* effective_type = action_type;
    if (!action_type || action_type[0] == '\0') {
        if (strncmp(action, "KC_MS_", 6) == 0) {
            if (strncmp(action + 6, "BTN", 3) == 0) {
                effective_type = "mouse_button";
            } else {
                effective_type = "special";
            }
        } else if (strncmp(action, "KC_SYSTEM_", 10) == 0) {
            effective_type = "system_control";
        } else if (strncmp(action, "KC_CURSOR_", 10) == 0) {
            effective_type = "special";
        } else if (strncmp(action, "KC_", 3) == 0 &&
                  (strncmp(action + 3, "AUDIO_", 6) == 0 ||
                   strncmp(action + 3, "MEDIA_", 6) == 0 ||
                   strncmp(action + 3, "WWW_", 4) == 0 ||
                   strncmp(action + 3, "BRIGHTNESS_", 11) == 0 ||
                   strcmp(action + 3, "MAIL") == 0 ||
                   strcmp(action + 3, "CALCULATOR") == 0 ||
                   strcmp(action + 3, "MY_COMPUTER") == 0)) {
            effective_type = "consumer_control";
        } else {
            effective_type = "keyboard_key";
        }
    }

    const cache_entry_t* cached = find_in_cache(effective_type, action);
    if (cached) {
        switch (cached->type) {
            case 1: // keyboard
                execute_keyboard_action(conn_id, cached->parsed.key, 
                    strcmp(effective_type, "keyboard_combo") == 0 ? 
                    string_to_modifiers(modifiers, modifier_count) : 0);
                return;
            case 2: // mouse
                execute_mouse_button_action(conn_id, cached->parsed.button);
                return;
            case 3: // system
                execute_system_control_action(conn_id, cached->parsed.system);
                return;
            case 4: // consumer
                execute_consumer_control_action(conn_id, cached->parsed.control);
                return;
            case 5: // special
                execute_special_action(conn_id, cached->parsed.special);
                return;
        }
    }

    if (strcmp(effective_type, "keyboard_key") == 0 || strcmp(effective_type, "keyboard_combo") == 0) {
        const keyboard_key_t key = string_to_keyboard_key(action);
        if (key) {
            add_to_cache(effective_type, action, 1, &key);
            execute_keyboard_action(conn_id, key, 
                strcmp(effective_type, "keyboard_combo") == 0 ? 
                string_to_modifiers(modifiers, modifier_count) : 0);
        }
    } else if (strcmp(effective_type, "mouse_button") == 0) {
        const mouse_button_t button = string_to_mouse_button(action);
        if (button) {
            add_to_cache(effective_type, action, 2, &button);
            execute_mouse_button_action(conn_id, button);
        }
    } else if (strcmp(effective_type, "system_control") == 0) {
        const system_control_t control = string_to_system_control(action);
        if (control) {
            add_to_cache(effective_type, action, 3, &control);
            execute_system_control_action(conn_id, control);
        }
    } else {
        const special_key_t special = string_to_special_key(action);
        if (special) {
            add_to_cache(effective_type, action, 5, &special);
            execute_special_action(conn_id, special);
            return;
        }

        const consumer_control_t consumer = string_to_consumer_control(action);
        if (consumer) {
            add_to_cache(effective_type, action, 4, &consumer);
            execute_consumer_control_action(conn_id, consumer);
            return;
        }

        ESP_LOGW(TAG, "Unknown action type or action: %s - %s", effective_type, action);
    }
}
