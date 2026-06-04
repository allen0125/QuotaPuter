#include "ui.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include <M5Unified.h>

#include "provider_style.h"
#include "quota_types.h"

namespace ui {
namespace {
inline M5GFX &gfx() { return M5.Display; }
}  // namespace

void init() {
    auto &d = gfx();
    d.setRotation(1);  // 240x135 landscape
    d.setColorDepth(16);
    d.setTextWrap(false);
    d.setBrightness(160);
    d.fillScreen(BLACK);
}

void clear(uint16_t color) { gfx().fillScreen(color); }

int text_width(const char *s, int size) {
    auto &d = gfx();
    d.setTextSize(size);
    return d.textWidth(s);
}

void text(int x, int y, const char *s, uint16_t fg, uint16_t bg, int size) {
    auto &d = gfx();
    d.setTextSize(size);
    d.setTextColor(fg, bg);
    d.setCursor(x, y);
    d.print(s);
}

void text_center(int y, const char *s, uint16_t fg, uint16_t bg, int size) {
    text((gfx().width() - text_width(s, size)) / 2, y, s, fg, bg, size);
}

void splash(const char *version) {
    auto &d = gfx();
    clear(BLACK);
    d.drawRect(0, 0, d.width(), d.height(), GREEN);
    text_center(40, "QUOTAPUTER", GREEN, BLACK, 2);
    char buf[24];
    snprintf(buf, sizeof(buf), "v%s", version);
    text_center(74, buf, WHITE, BLACK, 1);
    text_center(92, "LLM quota viewer", GRAY, BLACK, 1);
}

uint16_t status_color(int status, float used_pct, bool is_balance, float remaining) {
    switch (status) {
        case QUOTA_STATUS_OK:
            break;
        case QUOTA_STATUS_STALE:
        case QUOTA_STATUS_SETUP_REQUIRED:
            return GRAY;
        default:
            return RED;  // all error states
    }
    if (is_balance) {
        if (!std::isnan(remaining)) {
            if (remaining <= 0.0f) return RED;
            if (remaining < 5.0f) return YELLOW;
        }
        return GREEN;
    }
    if (!std::isnan(used_pct)) {
        if (used_pct >= 95.0f) return RED;
        if (used_pct >= 80.0f) return YELLOW;
    }
    return GREEN;
}

void title_bar(const char *title, bool wifi_connected, int rssi) {
    auto &d = gfx();
    d.fillRect(0, 0, d.width(), TITLE_H, DARK);
    text(4, 4, title, WHITE, DARK, 1);
    const char *w = wifi_connected ? "WIFI" : "OFF";
    uint16_t wc = wifi_connected ? GREEN : RED;
    int wx = d.width() - text_width(w, 1) - 4;
    text(wx, 4, w, wc, DARK, 1);
    (void)rssi;
}

void progress_bar(int x, int y, int w, int h, float pct, uint16_t color) {
    auto &d = gfx();
    if (std::isnan(pct)) pct = 0.0f;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    d.drawRect(x, y, w, h, WHITE);
    int fill = (int)((w - 2) * (pct / 100.0f) + 0.5f);
    if (fill > 0) {
        d.fillRect(x + 1, y + 1, fill, h - 2, color);
    }
    if (fill < w - 2) {
        d.fillRect(x + 1 + fill, y + 1, (w - 2) - fill, h - 2, BLACK);
    }
}

void status_badge(int x, int y, int status) {
    auto &d = gfx();
    const char *b = quota_status_badge(status);
    uint16_t c = (status == QUOTA_STATUS_OK)
                     ? GREEN
                     : (status == QUOTA_STATUS_STALE || status == QUOTA_STATUS_SETUP_REQUIRED)
                           ? GRAY
                           : RED;
    int w = text_width(b, 1) + 4;
    d.fillRect(x, y, w, 10, c);
    text(x + 2, y + 1, b, BLACK, c, 1);
}

void provider_logo(int x, int y, int size, const char *id, const char *region) {
    auto &d = gfx();
    provider_style_t st = provider_style_get(id);
    int r = size / 6;
    d.fillRoundRect(x, y, size, size, r, st.color);
    d.drawRoundRect(x, y, size, size, r, WHITE);

    int ts = size >= 24 ? 2 : 1;
    int gw = text_width(st.glyph, ts);
    int gh = 8 * ts;
    text(x + (size - gw) / 2, y + (size - gh) / 2, st.glyph, WHITE, st.color, ts);

    if (region != nullptr && region[0] != '\0') {
        int bw = text_width(region, 1) + 2;
        int bx = x + size - bw;
        int by = y + size - 9;
        d.fillRect(bx, by, bw, 9, BLACK);
        d.drawRect(bx, by, bw, 9, st.color);
        text(bx + 1, by + 1, region, WHITE, BLACK, 1);
    }
}

}  // namespace ui
