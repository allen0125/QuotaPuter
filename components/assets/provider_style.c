#include "provider_style.h"

#include <string.h>

// RGB565 accent colours + original glyphs (not vendor trademarks).
static const struct {
    const char *id;
    uint16_t color;
    const char *glyph;
} TABLE[] = {
    {"minimax_cn",     0x067A, "MM"},  // teal
    {"minimax_global", 0x067A, "MM"},
    {"openai",         0x0C6D, "AI"},  // green
    {"anthropic",      0xCAC6, "AN"},  // clay/coral
    {"gemini",         0x451F, "GE"},  // blue
    {"kimi",           0x8A1F, "KI"},  // violet
};

provider_style_t provider_style_get(const char *id) {
    provider_style_t def = {0x8410, "?"};
    if (id == NULL) {
        return def;
    }
    for (size_t i = 0; i < sizeof(TABLE) / sizeof(TABLE[0]); i++) {
        if (strcmp(TABLE[i].id, id) == 0) {
            provider_style_t s = {TABLE[i].color, TABLE[i].glyph};
            return s;
        }
    }
    return def;
}
