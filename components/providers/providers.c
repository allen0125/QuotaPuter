#include "providers.h"

#include "esp_log.h"
#include "provider_registry.h"

static const char *TAG = "providers";

void providers_register_all(void) {
    const quota_provider_t *all[] = {
        &minimax_cn_provider,
        &minimax_global_provider,
        // kimi + relay providers are registered as their modules land.
    };
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
        esp_err_t err = provider_registry_register(all[i]);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "register %s failed: %s", all[i]->id, esp_err_to_name(err));
        }
    }
    ESP_LOGI(TAG, "%u providers registered", (unsigned)provider_registry_count());
}
