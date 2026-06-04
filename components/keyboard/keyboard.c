#include "keyboard.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "keyboard";

// 74HC138 address pins (A0,A1,A2) and the 7 column inputs, in scan order.
static const int OUT_PINS[3] = {8, 9, 11};
static const int IN_PINS[7] = {13, 15, 3, 4, 5, 6, 7};

// Column index -> logical x, depending on the scan-address half (i>3 vs i<=3).
static const uint8_t X_HI[7] = {0, 2, 4, 6, 8, 10, 12};   // address 4..7
static const uint8_t X_LO[7] = {1, 3, 5, 7, 9, 11, 13};   // address 0..3

// Special key codes used in the keymap (from M5Cardputer Keyboard_def.h).
#define K_OPT 0x00
#define K_ENTER 0x28
#define K_BKSP 0x2a
#define K_TAB 0x2b
#define K_CTRL 0x80
#define K_SHIFT 0x81
#define K_ALT 0x82
#define K_FN 0xff

typedef struct {
    unsigned char first;   // unshifted
    unsigned char second;  // shifted
} keydef_t;

// 4 rows (y) x 14 columns (x), verbatim from M5Cardputer _key_value_map.
static const keydef_t KEYMAP[4][14] = {
    // y = 0 — number row
    {{'`', '~'}, {'1', '!'}, {'2', '@'}, {'3', '#'}, {'4', '$'}, {'5', '%'},
     {'6', '^'}, {'7', '&'}, {'8', '*'}, {'9', '('}, {'0', ')'}, {'-', '_'},
     {'=', '+'}, {K_BKSP, K_BKSP}},
    // y = 1 — QWERTY top row
    {{K_TAB, K_TAB}, {'q', 'Q'}, {'w', 'W'}, {'e', 'E'}, {'r', 'R'}, {'t', 'T'},
     {'y', 'Y'}, {'u', 'U'}, {'i', 'I'}, {'o', 'O'}, {'p', 'P'}, {'[', '{'},
     {']', '}'}, {'\\', '|'}},
    // y = 2 — home row
    {{K_FN, K_FN}, {K_SHIFT, K_SHIFT}, {'a', 'A'}, {'s', 'S'}, {'d', 'D'},
     {'f', 'F'}, {'g', 'G'}, {'h', 'H'}, {'j', 'J'}, {'k', 'K'}, {'l', 'L'},
     {';', ':'}, {'\'', '"'}, {K_ENTER, K_ENTER}},
    // y = 3 — bottom row
    {{K_CTRL, K_CTRL}, {K_OPT, K_OPT}, {K_ALT, K_ALT}, {'z', 'Z'}, {'x', 'X'},
     {'c', 'C'}, {'v', 'V'}, {'b', 'B'}, {'n', 'N'}, {'m', 'M'}, {',', '<'},
     {'.', '>'}, {'/', '?'}, {' ', ' '}},
};

#define LONG_PRESS_US 500000  // 500 ms

static QueueHandle_t s_queue;

static bool is_modifier(unsigned char code) {
    return code == K_FN || code == K_OPT || code == K_CTRL || code == K_SHIFT ||
           code == K_ALT;
}

static void set_address(int value) {
    gpio_set_level(OUT_PINS[0], value & 0x01);
    gpio_set_level(OUT_PINS[1], (value >> 1) & 0x01);
    gpio_set_level(OUT_PINS[2], (value >> 2) & 0x01);
}

// Scan the whole matrix into pressed[y][x] (true = key down).
static void scan_matrix(bool pressed[4][14]) {
    memset(pressed, 0, sizeof(bool) * 4 * 14);
    for (int i = 0; i < 8; i++) {
        set_address(i);
        esp_rom_delay_us(8);  // let the decoder + lines settle
        for (int j = 0; j < 7; j++) {
            if (gpio_get_level(IN_PINS[j]) == 0) {  // active-low: pressed
                int x = (i > 3) ? X_HI[j] : X_LO[j];
                int y = 3 - (i & 0x03);
                pressed[y][x] = true;
            }
        }
    }
}

static void emit(int y, int x, bool fn, bool sh, bool ct, bool al, bool op, bool longp) {
    unsigned char code = sh ? KEYMAP[y][x].second : KEYMAP[y][x].first;
    kb_event_t ev = {0};
    ev.fn = fn;
    ev.shift = sh;
    ev.ctrl = ct;
    ev.alt = al;
    ev.opt = op;
    ev.long_press = longp;
    if (code == K_ENTER) {
        ev.enter = true;
    } else if (code == K_BKSP) {
        ev.del = true;
    } else if (code == K_TAB) {
        ev.tab = true;
    } else if (code >= 0x20 && code < 0x7f) {
        ev.ch = (char)code;
        if (code == ' ') ev.space = true;
    } else {
        return;  // unmapped / modifier slipped through — ignore
    }
    xQueueSend(s_queue, &ev, 0);
}

static void keyboard_task(void *arg) {
    (void)arg;
    bool prev[4][14];
    bool longed[4][14];
    int64_t start[4][14];
    memset(prev, 0, sizeof(prev));
    memset(longed, 0, sizeof(longed));
    memset(start, 0, sizeof(start));

    for (;;) {
        bool now[4][14];
        scan_matrix(now);

        // Resolve modifier state for this scan first.
        bool fn = now[2][0], shift = now[2][1], ctrl = now[3][0];
        bool opt = now[3][1], alt = now[3][2];

        int64_t t = esp_timer_get_time();
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 14; x++) {
                unsigned char code = KEYMAP[y][x].first;
                bool mod = is_modifier(code);
                if (now[y][x] && !prev[y][x]) {
                    start[y][x] = t;
                    longed[y][x] = false;
                    if (!mod) emit(y, x, fn, shift, ctrl, alt, opt, false);
                } else if (now[y][x] && prev[y][x]) {
                    if (!mod && !longed[y][x] && (t - start[y][x]) >= LONG_PRESS_US) {
                        longed[y][x] = true;
                        emit(y, x, fn, shift, ctrl, alt, opt, true);
                    }
                } else if (!now[y][x]) {
                    longed[y][x] = false;
                }
                prev[y][x] = now[y][x];
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

esp_err_t keyboard_init(void) {
    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << OUT_PINS[0]) | (1ULL << OUT_PINS[1]) | (1ULL << OUT_PINS[2]),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&out_cfg));
    for (int i = 0; i < 3; i++) {
        gpio_set_level(OUT_PINS[i], 0);
    }

    uint64_t in_mask = 0;
    for (int i = 0; i < 7; i++) {
        in_mask |= (1ULL << IN_PINS[i]);
    }
    gpio_config_t in_cfg = {
        .pin_bit_mask = in_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,  // pressed key pulls the column LOW
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in_cfg));

    s_queue = xQueueCreate(16, sizeof(kb_event_t));
    if (s_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(keyboard_task, "keyboard", 3072, NULL, 6, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "74HC138 matrix keyboard ready");
    return ESP_OK;
}

bool keyboard_poll(kb_event_t *ev) {
    if (s_queue == NULL || ev == NULL) {
        return false;
    }
    return xQueueReceive(s_queue, ev, 0) == pdTRUE;
}
