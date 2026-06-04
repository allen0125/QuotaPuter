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

#include "keyboard.h"
#include "provider_registry.h"
#include "providers.h"
#include "provisioner.h"
#include "secret_store.h"
#include "ui.h"
#include "wifi_manager.h"

#define QUOTAPUTER_VERSION "0.1.0"

namespace {
constexpr char TAG[] = "quotaputer";
}  // namespace

extern "C" void app_main(void) {
    auto cfg = M5.config();
    M5.begin(cfg);
    ui::init();
    ui::splash(QUOTAPUTER_VERSION);

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

    // USB-Serial-JTAG provisioning protocol (PC config tool talks to this).
    ESP_ERROR_CHECK(provisioner_start());

    // Cardputer keyboard (after M5.begin so the display init keeps off our pins).
    ESP_ERROR_CHECK(keyboard_init());

    ESP_LOGI(TAG, "QuotaPuter v%s booting (display %dx%d, %u providers, wifi=%s)",
             QUOTAPUTER_VERSION, (int)M5.Display.width(), (int)M5.Display.height(),
             (unsigned)provider_registry_count(),
             secret_store_has_wifi() ? "set" : "unset");

    for (;;) {
        M5.update();
        kb_event_t ev;
        while (keyboard_poll(&ev)) {
            if (ev.ch != 0) {
                ESP_LOGI(TAG, "key '%c'%s%s", ev.ch, ev.shift ? " +shift" : "",
                         ev.long_press ? " (long)" : "");
            } else if (ev.enter) {
                ESP_LOGI(TAG, "ENTER");
            } else if (ev.del) {
                ESP_LOGI(TAG, "DEL%s%s", ev.fn ? " +fn" : "", ev.long_press ? " (long)" : "");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
