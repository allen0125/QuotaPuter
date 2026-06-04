// ui.h — pixel-art UI primitives drawn on the Cardputer display (PRD §8).
//
// Thin C++ helpers over M5GFX (M5.Display). Screens in main/ compose these into
// the overview/detail/settings pages. All drawing is direct-to-screen (no full
// frame buffer) to stay within internal RAM on the PSRAM-less Cardputer.
#pragma once

#include <stdint.h>

namespace ui {

// RGB565 palette (PRD §8.4 status colours + retro chrome).
constexpr uint16_t BLACK = 0x0000;
constexpr uint16_t WHITE = 0xFFFF;
constexpr uint16_t GREEN = 0x07E0;   // OK
constexpr uint16_t YELLOW = 0xFFE0;  // >80% used
constexpr uint16_t RED = 0xF800;     // >95% used / failure
constexpr uint16_t GRAY = 0x8410;    // stale / setup
constexpr uint16_t DARK = 0x18C3;    // title bar / chrome
constexpr uint16_t DIM = 0x52AA;     // secondary text

constexpr int TITLE_H = 16;  // title-bar height

void init();
void clear(uint16_t color = BLACK);
void splash(const char *version);

// Status -> colour. For balance metrics pass is_balance=true and the remaining
// balance; otherwise pass the used percentage.
uint16_t status_color(int status, float used_pct, bool is_balance, float remaining);

// Top title bar with a Wi-Fi indicator at the right.
void title_bar(const char *title, bool wifi_connected, int rssi);

// Bordered pixel progress bar; pct is 0..100 (clamped).
void progress_bar(int x, int y, int w, int h, float pct, uint16_t color);

// Small coloured status badge ("OK"/"ERR"/"STALE"/"SETUP").
void status_badge(int x, int y, int status);

// Original provider identification glyph + optional region badge (CN/GL).
void provider_logo(int x, int y, int size, const char *id, const char *region);

// Text helpers (default 6x8 font scaled by `size`).
void text(int x, int y, const char *s, uint16_t fg, uint16_t bg = BLACK, int size = 1);
void text_center(int y, const char *s, uint16_t fg, uint16_t bg = BLACK, int size = 1);
int text_width(const char *s, int size = 1);

}  // namespace ui
