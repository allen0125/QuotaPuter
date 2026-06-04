// app.cpp — QuotaPuter UI state machine and screens (PRD §5, §8, §9).
//
// Screens: overview carousel, provider detail, settings, Wi-Fi entry, provider
// management, device info / secure wipe. Input comes from the keyboard event
// queue; the Cardputer has no dedicated arrows, so `,`/`/`/`;`/`.` act as
// left/right/up/down and Backspace acts as Back (PRD §9). Data comes from the
// refresh manager.

#include "app.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <M5Unified.h>

#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "keyboard.h"
#include "provider_registry.h"
#include "quota_types.h"
#include "refresh.h"
#include "result_cache.h"
#include "secret_store.h"
#include "ui.h"
#include "wifi_manager.h"

namespace {

enum Screen { SCR_OVERVIEW, SCR_DETAIL, SCR_SETTINGS, SCR_WIFI, SCR_MANAGE, SCR_DEVICE };

struct State {
    Screen screen = SCR_OVERVIEW;
    int sel = 0;       // selected visible provider (overview/detail)
    int menu_sel = 0;  // selection within settings/manage
    int field = 0;     // Wi-Fi entry: 0=ssid, 1=password
    char ssid[SECRET_STORE_SSID_MAXLEN] = {0};
    char pass[SECRET_STORE_PASS_MAXLEN] = {0};
};

State st;
const char *s_version = "0.0.0";

constexpr int SETTINGS_ITEMS = 5;
const char *SETTINGS_LABELS[SETTINGS_ITEMS] = {
    "Refresh interval", "Wi-Fi setup", "Manage providers", "Device info", "Back"};
const int INTERVALS[4] = {1, 5, 15, 30};

bool is_balance(const refresh::Entry &e) {
    return strcmp(e.prov->metric_type, QUOTA_METRIC_BALANCE) == 0;
}
bool is_subscription(const refresh::Entry &e) {
    return strcmp(e.prov->metric_type, QUOTA_METRIC_SUBSCRIPTION) == 0;
}

int disp_status(const refresh::Entry &e) {
    if (!e.configured) return QUOTA_STATUS_SETUP_REQUIRED;
    if (!e.have_data) return e.last_status;
    return e.result.status;
}

void fmt_primary(const refresh::Entry &e, char *buf, size_t n) {
    const quota_result_t &r = e.result;
    if (!e.configured) {
        snprintf(buf, n, "SETUP");
        return;
    }
    if (!e.have_data) {
        snprintf(buf, n, "%s", e.refreshing ? "SYNC..." : quota_status_message(e.last_status));
        return;
    }
    if (is_subscription(e)) {
        if (!std::isnan(r.percentage)) {
            snprintf(buf, n, "%.0f%% LEFT", 100.0f - r.percentage);
        } else {
            snprintf(buf, n, "OK");
        }
    } else if (is_balance(e)) {
        if (!std::isnan(r.remaining)) {
            snprintf(buf, n, "%.2f %s", r.remaining, r.unit);
        } else {
            snprintf(buf, n, "--");
        }
    } else {  // usage / project
        if (!std::isnan(r.used)) {
            snprintf(buf, n, "%.2f %s", r.used, r.unit);
        } else if (!std::isnan(r.percentage)) {
            snprintf(buf, n, "%.0f%%", r.percentage);
        } else {
            snprintf(buf, n, "OK");
        }
    }
}

void fmt_ago(int64_t updated, char *buf, size_t n) {
    if (updated <= 0) {
        snprintf(buf, n, "never");
        return;
    }
    int64_t d = (int64_t)time(NULL) - updated;
    if (d < 0) d = 0;
    if (d < 60) {
        snprintf(buf, n, "%llds ago", (long long)d);
    } else if (d < 3600) {
        snprintf(buf, n, "%lldm ago", (long long)(d / 60));
    } else if (d < 86400) {
        snprintf(buf, n, "%lldh ago", (long long)(d / 3600));
    } else {
        snprintf(buf, n, "%lldd ago", (long long)(d / 86400));
    }
}

uint16_t entry_color(const refresh::Entry &e) {
    return ui::status_color(disp_status(e), e.result.percentage, is_balance(e),
                            e.result.remaining);
}

void toast(const char *msg, uint16_t color) {
    int w = ui::text_width(msg, 1) + 16;
    int x = (ui::width() - w) / 2;
    ui::fill_rect(x, 54, w, 22, ui::DARK);
    ui::draw_rect(x, 54, w, 22, color);
    ui::text(x + 8, 61, msg, color, ui::DARK, 1);
    ui::present();
    vTaskDelay(pdMS_TO_TICKS(900));
}

// ---- screens: render --------------------------------------------------------

void render_overview() {
    ui::clear();
    ui::title_bar("QUOTAPUTER", wifi_manager_is_connected(), wifi_manager_rssi());

    int total = refresh::visible_count();
    if (total == 0) {
        ui::text_center(46, "NO PROVIDERS ENABLED", ui::GRAY, ui::BLACK, 1);
        ui::text_center(64, "Press S for settings, or", ui::DIM, ui::BLACK, 1);
        ui::text_center(76, "add via quota_config.py", ui::DIM, ui::BLACK, 1);
        ui::text_center(112, ",/  switch  ENTER detail  R refresh", ui::DIM, ui::BLACK, 1);
        return;
    }
    if (st.sel >= total) st.sel = total - 1;
    if (st.sel < 0) st.sel = 0;
    refresh::Entry e;
    if (!refresh::get_visible(st.sel, &e)) return;
    uint16_t col = entry_color(e);

    const int cx = 6, cy = 20, cw = 228, ch = 86;
    ui::draw_rect(cx, cy, cw, ch, col);
    ui::provider_logo(cx + 8, cy + 10, 28, e.prov->id, e.prov->region);
    ui::text(cx + 44, cy + 8, e.prov->display_name, ui::WHITE, ui::BLACK, 1);
    ui::status_badge(cx + cw - 44, cy + 8, disp_status(e));

    char buf[48];
    fmt_primary(e, buf, sizeof(buf));
    ui::text(cx + 44, cy + 24, buf, col, ui::BLACK, 2);

    if ((is_subscription(e) || e.result.has_limit) && !std::isnan(e.result.percentage)) {
        ui::progress_bar(cx + 8, cy + 56, cw - 16, 10, e.result.percentage, col);
    }
    char ago[24];
    fmt_ago(e.result.updated_at, ago, sizeof(ago));
    ui::text(cx + 8, cy + 72, ago, ui::DIM, ui::BLACK, 1);

    char pos[16];
    snprintf(pos, sizeof(pos), "%d/%d", st.sel + 1, total);
    ui::text(cx + cw - ui::text_width(pos, 1) - 8, cy + 72, pos, ui::WHITE, ui::BLACK, 1);

    ui::text_center(114, ",/  switch  ENTER detail  R S W", ui::DIM, ui::BLACK, 1);
}

void render_detail() {
    refresh::Entry e;
    if (!refresh::get_visible(st.sel, &e)) {
        st.screen = SCR_OVERVIEW;
        return;
    }
    ui::clear();
    ui::title_bar(e.prov->display_name, wifi_manager_is_connected(), wifi_manager_rssi());
    ui::provider_logo(6, 20, 30, e.prov->id, e.prov->region);
    ui::text(42, 20, e.prov->display_name, ui::WHITE, ui::BLACK, 1);
    if (e.prov->disclaimer != nullptr) {
        ui::text(42, 32, e.prov->disclaimer, ui::DIM, ui::BLACK, 1);
    }
    ui::status_badge(190, 20, disp_status(e));

    uint16_t col = entry_color(e);
    int y = 54;
    char buf[48];
    fmt_primary(e, buf, sizeof(buf));
    ui::text(8, y, buf, col, ui::BLACK, 2);
    y += 24;

    if ((is_subscription(e) || e.result.has_limit) && !std::isnan(e.result.percentage)) {
        ui::progress_bar(8, y, 180, 10, e.result.percentage, col);
        y += 14;
        if (e.result.has_limit && !std::isnan(e.result.used)) {
            snprintf(buf, sizeof(buf), "%.0f / %.0f %s", e.result.used, e.result.limit,
                     e.result.unit);
            ui::text(8, y, buf, ui::WHITE, ui::BLACK, 1);
            y += 12;
        }
    }
    // Secondary metrics: Kimi cash/voucher, MiniMax interval/weekly remaining %.
    if (!std::isnan(e.result.aux1) && e.result.aux1_label[0] != '\0') {
        snprintf(buf, sizeof(buf), "%s: %.2f", e.result.aux1_label, e.result.aux1);
        ui::text(8, y, buf, ui::WHITE, ui::BLACK, 1);
        y += 12;
    }
    if (!std::isnan(e.result.aux2) && e.result.aux2_label[0] != '\0') {
        snprintf(buf, sizeof(buf), "%s: %.2f", e.result.aux2_label, e.result.aux2);
        ui::text(8, y, buf, ui::WHITE, ui::BLACK, 1);
        y += 12;
    }
    if (e.result.reset_at > 0) {
        int64_t d = e.result.reset_at - (int64_t)time(NULL);
        if (d <= 0) {
            snprintf(buf, sizeof(buf), "RESET DUE");
        } else if (d < 3600) {
            snprintf(buf, sizeof(buf), "RESETS IN %lldm", (long long)(d / 60));
        } else if (d < 86400) {
            snprintf(buf, sizeof(buf), "RESETS IN %lldh", (long long)(d / 3600));
        } else {
            snprintf(buf, sizeof(buf), "RESETS IN %lldd", (long long)(d / 86400));
        }
        ui::text(8, y, buf, ui::DIM, ui::BLACK, 1);
        y += 12;
    }
    if (disp_status(e) != QUOTA_STATUS_OK) {
        ui::text(8, y, quota_status_message(disp_status(e)),
                 disp_status(e) == QUOTA_STATUS_STALE ? ui::GRAY : ui::RED, ui::BLACK, 1);
    }

    char ago[24];
    fmt_ago(e.result.updated_at, ago, sizeof(ago));
    ui::text(8, 118, ago, ui::DIM, ui::BLACK, 1);
    ui::text(132, 118, "R refr  BKSP back", ui::DIM, ui::BLACK, 1);
}

void render_settings() {
    ui::clear();
    ui::title_bar("SETTINGS", wifi_manager_is_connected(), wifi_manager_rssi());
    for (int i = 0; i < SETTINGS_ITEMS; i++) {
        int y = 24 + i * 16;
        uint16_t fg = (i == st.menu_sel) ? ui::BLACK : ui::WHITE;
        uint16_t bg = (i == st.menu_sel) ? ui::GREEN : ui::BLACK;
        if (i == st.menu_sel) ui::fill_rect(4, y - 2, 232, 14, ui::GREEN);
        char line[48];
        if (i == 0) {
            snprintf(line, sizeof(line), "%s: %d min", SETTINGS_LABELS[0],
                     refresh::interval_minutes());
        } else {
            snprintf(line, sizeof(line), "%s", SETTINGS_LABELS[i]);
        }
        ui::text(8, y, line, fg, bg, 1);
    }
    ui::text_center(118, ";/. nav  ENTER sel  ,// rate  BKSP back", ui::DIM, ui::BLACK, 1);
}

void render_wifi() {
    ui::clear();
    ui::title_bar("WI-FI SETUP", wifi_manager_is_connected(), wifi_manager_rssi());
    ui::text(8, 28, "SSID:", ui::WHITE, ui::BLACK, 1);
    uint16_t ssid_col = st.field == 0 ? ui::GREEN : ui::WHITE;
    ui::draw_rect(8, 40, 224, 16, ssid_col);
    ui::text(12, 44, st.ssid, ui::WHITE, ui::BLACK, 1);

    ui::text(8, 64, "PASSWORD:", ui::WHITE, ui::BLACK, 1);
    uint16_t pass_col = st.field == 1 ? ui::GREEN : ui::WHITE;
    ui::draw_rect(8, 76, 224, 16, pass_col);
    char masked[SECRET_STORE_PASS_MAXLEN];
    size_t pl = strlen(st.pass);
    for (size_t i = 0; i < pl && i < sizeof(masked) - 1; i++) masked[i] = '*';
    masked[pl < sizeof(masked) - 1 ? pl : sizeof(masked) - 1] = '\0';
    ui::text(12, 80, masked, ui::WHITE, ui::BLACK, 1);

    ui::text_center(112, "type  ENTER next/save  BKSP del/back", ui::DIM, ui::BLACK, 1);
}

void render_manage() {
    ui::clear();
    ui::title_bar("PROVIDERS", wifi_manager_is_connected(), wifi_manager_rssi());
    int n = (int)provider_registry_count();
    for (int i = 0; i < n; i++) {
        const quota_provider_t *p = provider_registry_get(i);
        int y = 22 + i * 15;
        if (i == st.menu_sel) ui::fill_rect(4, y - 2, 232, 13, ui::DARK);
        bool configured = secret_store_provider_configured(p->id);
        bool enabled = secret_store_provider_enabled(p->id);
        const char *mark = enabled ? "[x]" : (configured ? "[ ]" : "[-]");
        uint16_t col = enabled ? ui::GREEN : (configured ? ui::WHITE : ui::GRAY);
        char line[48];
        snprintf(line, sizeof(line), "%s %-15s %s", mark, p->display_name,
                 configured ? "" : "(setup)");
        ui::text(8, y, line, col, i == st.menu_sel ? ui::DARK : ui::BLACK, 1);
    }
    ui::text_center(118, "ENTER on/off  D=del  BKSP back", ui::DIM, ui::BLACK, 1);
}

void render_device() {
    ui::clear();
    ui::title_bar("DEVICE", wifi_manager_is_connected(), wifi_manager_rssi());
    int configured = 0;
    for (size_t i = 0; i < provider_registry_count(); i++) {
        if (secret_store_provider_configured(provider_registry_get(i)->id)) configured++;
    }
    char line[48];
    snprintf(line, sizeof(line), "QuotaPuter v%s", s_version);
    ui::text(8, 26, line, ui::GREEN, ui::BLACK, 1);
    snprintf(line, sizeof(line), "Wi-Fi: %s", wifi_manager_is_connected() ? "connected" : "down");
    ui::text(8, 42, line, ui::WHITE, ui::BLACK, 1);
    if (wifi_manager_is_connected()) {
        snprintf(line, sizeof(line), "RSSI: %d dBm", wifi_manager_rssi());
        ui::text(8, 54, line, ui::WHITE, ui::BLACK, 1);
    }
    snprintf(line, sizeof(line), "Providers configured: %d", configured);
    ui::text(8, 66, line, ui::WHITE, ui::BLACK, 1);
    snprintf(line, sizeof(line), "Free heap: %u KB",
             (unsigned)(esp_get_free_heap_size() / 1024));
    ui::text(8, 78, line, ui::WHITE, ui::BLACK, 1);
    ui::text(8, 98, "Fn+Del (hold): WIPE ALL", ui::RED, ui::BLACK, 1);
    ui::text_center(118, "BKSP back", ui::DIM, ui::BLACK, 1);
}

void render() {
    switch (st.screen) {
        case SCR_OVERVIEW: render_overview(); break;
        case SCR_DETAIL:   render_detail();   break;
        case SCR_SETTINGS: render_settings(); break;
        case SCR_WIFI:     render_wifi();     break;
        case SCR_MANAGE:   render_manage();   break;
        case SCR_DEVICE:   render_device();   break;
    }
    ui::present();  // push the composed frame in one blit (no flicker)
}

// ---- input ------------------------------------------------------------------

enum Nav { NAV_NONE, NAV_LEFT, NAV_RIGHT, NAV_UP, NAV_DOWN };
Nav nav_of(const kb_event_t &e) {
    switch (e.ch) {
        case ',': return NAV_LEFT;
        case '/': return NAV_RIGHT;
        case ';': return NAV_UP;
        case '.': return NAV_DOWN;
        default:  return NAV_NONE;
    }
}

bool wipe_combo(const kb_event_t &e) { return e.del && e.fn && e.long_press; }

void handle_overview(const kb_event_t &e) {
    int total = refresh::visible_count();
    if (wipe_combo(e)) {
        secret_store_factory_reset();
        result_cache_clear_all();
        wifi_manager_clear();
        toast("ALL CONFIG WIPED", ui::RED);
        return;
    }
    Nav nav = nav_of(e);
    if (total > 0 && nav == NAV_LEFT) {
        st.sel = (st.sel - 1 + total) % total;
    } else if (total > 0 && nav == NAV_RIGHT) {
        st.sel = (st.sel + 1) % total;
    } else if (e.enter && total > 0) {
        st.screen = SCR_DETAIL;
    } else if ((e.ch == 'r' || e.ch == 'R') && total > 0) {
        refresh::Entry en;
        if (refresh::get_visible(st.sel, &en)) refresh::request(en.prov->id);
        toast("REFRESHING", ui::GREEN);
    } else if (e.ch == 's' || e.ch == 'S') {
        st.screen = SCR_SETTINGS;
        st.menu_sel = 0;
    } else if (e.ch == 'w' || e.ch == 'W') {
        st.screen = SCR_WIFI;
        st.field = 0;
        st.ssid[0] = '\0';
        st.pass[0] = '\0';
    } else if ((e.ch == 'd' || e.ch == 'D') && e.long_press && total > 0) {
        refresh::Entry en;
        if (refresh::get_visible(st.sel, &en)) {
            secret_store_erase_provider(en.prov->id);
            result_cache_erase(en.prov->id);
            toast("PROVIDER DELETED", ui::YELLOW);
        }
    }
}

void handle_detail(const kb_event_t &e) {
    int total = refresh::visible_count();
    if (wipe_combo(e)) {
        secret_store_factory_reset();
        result_cache_clear_all();
        wifi_manager_clear();
        st.screen = SCR_OVERVIEW;
        toast("ALL CONFIG WIPED", ui::RED);
        return;
    }
    if (e.del) {
        st.screen = SCR_OVERVIEW;
    } else if (e.ch == 'r' || e.ch == 'R') {
        refresh::Entry en;
        if (refresh::get_visible(st.sel, &en)) refresh::request(en.prov->id);
        toast("REFRESHING", ui::GREEN);
    } else if (total > 0 && nav_of(e) == NAV_LEFT) {
        st.sel = (st.sel - 1 + total) % total;
    } else if (total > 0 && nav_of(e) == NAV_RIGHT) {
        st.sel = (st.sel + 1) % total;
    }
}

void handle_settings(const kb_event_t &e) {
    Nav nav = nav_of(e);
    if (e.del) {
        st.screen = SCR_OVERVIEW;
    } else if (nav == NAV_UP) {
        st.menu_sel = (st.menu_sel - 1 + SETTINGS_ITEMS) % SETTINGS_ITEMS;
    } else if (nav == NAV_DOWN) {
        st.menu_sel = (st.menu_sel + 1) % SETTINGS_ITEMS;
    } else if (st.menu_sel == 0 && (nav == NAV_LEFT || nav == NAV_RIGHT)) {
        int cur = refresh::interval_minutes();
        int idx = 0;
        for (int i = 0; i < 4; i++)
            if (INTERVALS[i] == cur) idx = i;
        idx = (nav == NAV_RIGHT) ? (idx + 1) % 4 : (idx + 3) % 4;
        refresh::set_interval_minutes(INTERVALS[idx]);
    } else if (e.enter) {
        switch (st.menu_sel) {
            case 1:
                st.screen = SCR_WIFI;
                st.field = 0;
                st.ssid[0] = '\0';
                st.pass[0] = '\0';
                break;
            case 2: st.screen = SCR_MANAGE; st.menu_sel = 0; break;
            case 3: st.screen = SCR_DEVICE; break;
            case 4: st.screen = SCR_OVERVIEW; break;
            default: break;
        }
    }
}

void append_char(char *buf, size_t cap, char c) {
    size_t l = strlen(buf);
    if (l < cap - 1) {
        buf[l] = c;
        buf[l + 1] = '\0';
    }
}

void handle_wifi(const kb_event_t &e) {
    char *buf = st.field == 0 ? st.ssid : st.pass;
    size_t cap = st.field == 0 ? sizeof(st.ssid) : sizeof(st.pass);
    if (e.enter) {
        if (st.field == 0) {
            if (st.ssid[0] != '\0') st.field = 1;
        } else {
            secret_store_save_wifi(st.ssid, st.pass);
            wifi_manager_connect(st.ssid, st.pass);
            st.screen = SCR_OVERVIEW;
            toast("WI-FI SAVED", ui::GREEN);
        }
    } else if (e.del) {
        size_t l = strlen(buf);
        if (l > 0) {
            buf[l - 1] = '\0';
        } else if (st.field == 1) {
            st.field = 0;
        } else {
            st.screen = SCR_SETTINGS;  // cancel from empty SSID
        }
    } else if (e.ch != 0) {
        append_char(buf, cap, e.ch);
    }
}

void handle_manage(const kb_event_t &e) {
    int n = (int)provider_registry_count();
    Nav nav = nav_of(e);
    if (e.del) {
        st.screen = SCR_SETTINGS;
        st.menu_sel = 2;
    } else if (nav == NAV_UP) {
        st.menu_sel = (st.menu_sel - 1 + n) % n;
    } else if (nav == NAV_DOWN) {
        st.menu_sel = (st.menu_sel + 1) % n;
    } else if (e.enter) {
        const quota_provider_t *p = provider_registry_get(st.menu_sel);
        if (secret_store_provider_configured(p->id)) {
            secret_store_set_enabled(p->id, !secret_store_provider_enabled(p->id));
        } else {
            toast("NOT CONFIGURED", ui::YELLOW);
        }
    } else if ((e.ch == 'd' || e.ch == 'D') && e.long_press) {
        const quota_provider_t *p = provider_registry_get(st.menu_sel);
        secret_store_erase_provider(p->id);
        result_cache_erase(p->id);
        toast("PROVIDER DELETED", ui::YELLOW);
    }
}

void handle_device(const kb_event_t &e) {
    if (wipe_combo(e)) {
        secret_store_factory_reset();
        result_cache_clear_all();
        wifi_manager_clear();
        st.screen = SCR_OVERVIEW;
        toast("ALL CONFIG WIPED", ui::RED);
    } else if (e.del) {
        st.screen = SCR_SETTINGS;
        st.menu_sel = 3;
    }
}

void handle_event(const kb_event_t &e) {
    switch (st.screen) {
        case SCR_OVERVIEW: handle_overview(e); break;
        case SCR_DETAIL:   handle_detail(e);   break;
        case SCR_SETTINGS: handle_settings(e); break;
        case SCR_WIFI:     handle_wifi(e);     break;
        case SCR_MANAGE:   handle_manage(e);   break;
        case SCR_DEVICE:   handle_device(e);   break;
    }
}

}  // namespace

void app_run(const char *version) {
    s_version = version;
    refresh::init();
    vTaskDelay(pdMS_TO_TICKS(1200));  // let the splash linger
    st.screen = SCR_OVERVIEW;
    render();

    int64_t last_tick_ms = 0;
    for (;;) {
        M5.update();
        bool dirty = false;
        kb_event_t ev;
        while (keyboard_poll(&ev)) {
            handle_event(ev);
            dirty = true;
        }
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms - last_tick_ms > 1000) {
            last_tick_ms = now_ms;
            // Live screens reflect refresh progress / relative times each second.
            if (st.screen == SCR_OVERVIEW || st.screen == SCR_DETAIL ||
                st.screen == SCR_DEVICE) {
                dirty = true;
            }
        }
        if (dirty) render();
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
