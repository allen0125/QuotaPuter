// QuotaPuter — firmware entry point.
//
// ESP-IDF's startup calls C `app_main`; we bridge into the C++ UI layer
// (M5Unified / M5GFX) from here. Data/network/storage modules are plain C and
// are wired in across later phases (see AGENT.md development plan). For now this
// brings up the display and shows the boot splash so Phase 0 is verifiable on
// real hardware.

#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <M5Unified.h>

#include "provider_registry.h"
#include "providers.h"
#include "secret_store.h"
#include "wifi_manager.h"

#define QUOTAPUTER_VERSION "0.1.0"

namespace {
constexpr char TAG[] = "quotaputer";

// RGB565 literals (avoid relying on Arduino TFT_* macros under pure ESP-IDF).
constexpr uint16_t COL_BLACK = 0x0000;
constexpr uint16_t COL_GREEN = 0x07E0;
constexpr uint16_t COL_WHITE = 0xFFFF;
constexpr uint16_t COL_GRAY = 0x8410;

void draw_splash() {
    auto &lcd = M5.Display;
    lcd.setRotation(1);  // 240x135 landscape
    lcd.setColorDepth(16);
    lcd.fillScreen(COL_BLACK);

    lcd.setTextColor(COL_GREEN, COL_BLACK);
    lcd.setTextSize(2);
    lcd.setCursor(28, 44);
    lcd.print("QUOTAPUTER");

    lcd.setTextSize(1);
    lcd.setTextColor(COL_WHITE, COL_BLACK);
    lcd.setCursor(28, 70);
    lcd.printf("v%s", QUOTAPUTER_VERSION);

    lcd.setTextColor(COL_GRAY, COL_BLACK);
    lcd.setCursor(28, 86);
    lcd.print("LLM quota viewer");

    // Simple framed pixel border to set the retro tone.
    lcd.drawRect(0, 0, lcd.width(), lcd.height(), COL_GREEN);
}
}  // namespace

extern "C" void app_main(void) {
    auto cfg = M5.config();
    M5.begin(cfg);
    // Persistent storage: default NVS (used by the Wi-Fi stack and app prefs)
    // plus the dedicated secret partition for credentials.
    esp_err_t nverr = nvs_flash_init();
    if (nverr == ESP_ERR_NVS_NO_FREE_PAGES || nverr == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nverr = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nverr);
    ESP_ERROR_CHECK(secret_store_init());

    // Register all providers into the registry.
    providers_register_all();

    // Networking: bring up Wi-Fi and connect with saved credentials if any.
    ESP_ERROR_CHECK(wifi_manager_init());
    if (secret_store_has_wifi()) {
        wifi_manager_start_saved();
    }

    ESP_LOGI(TAG, "QuotaPuter v%s booting (display %dx%d, %u providers, wifi=%s)",
             QUOTAPUTER_VERSION, (int)M5.Display.width(), (int)M5.Display.height(),
             (unsigned)provider_registry_count(),
             secret_store_has_wifi() ? "set" : "unset");

    draw_splash();

    for (;;) {
        M5.update();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
