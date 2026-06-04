#include "secret_store.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#if defined(CONFIG_NVS_ENCRYPTION)
#include "esp_partition.h"
#endif

#define SECRET_PARTITION "secret"
#define WIFI_NS "wifi"

// Per-provider keys (NVS keys are <=15 chars). Provider id is the namespace.
#define K_ENABLED "en"
#define K_MODE "mode"
#define K_SECRET "secret"
#define K_RELAY_URL "rurl"
#define K_RELAY_TOK "rtok"

static const char *TAG = "secret_store";

static esp_err_t open_ns(const char *ns, nvs_open_mode_t mode, nvs_handle_t *h) {
    return nvs_open_from_partition(SECRET_PARTITION, ns, mode, h);
}

static esp_err_t init_plaintext(void) {
    esp_err_t err = nvs_flash_init_partition(SECRET_PARTITION);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "secret partition needs erase (%s), reformatting", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase_partition(SECRET_PARTITION));
        err = nvs_flash_init_partition(SECRET_PARTITION);
    }
    return err;
}

esp_err_t secret_store_init(void) {
#if defined(CONFIG_NVS_ENCRYPTION)
    // Open the secret partition with NVS encryption using keys held in the
    // dedicated nvs_keys partition (must be present in the partition table).
    const esp_partition_t *key_part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS, NULL);
    if (key_part != NULL) {
        nvs_sec_cfg_t cfg;
        esp_err_t err = nvs_flash_read_security_cfg(key_part, &cfg);
        if (err == ESP_ERR_NVS_KEYS_NOT_INITIALIZED) {
            err = nvs_flash_generate_keys(key_part, &cfg);
        }
        if (err == ESP_OK) {
            err = nvs_flash_secure_init_partition(SECRET_PARTITION, &cfg);
            if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
                ESP_ERROR_CHECK(nvs_flash_erase_partition(SECRET_PARTITION));
                err = nvs_flash_secure_init_partition(SECRET_PARTITION, &cfg);
            }
            ESP_LOGI(TAG, "secret store: encrypted NVS ready");
            return err;
        }
        ESP_LOGW(TAG, "NVS key load failed (%s); falling back to plaintext",
                 esp_err_to_name(err));
    } else {
        ESP_LOGW(TAG, "no nvs_keys partition; using plaintext (enable flash encryption)");
    }
#endif
    return init_plaintext();
}

esp_err_t secret_store_load_provider(const char *id, provider_secret_t *out) {
    if (id == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    nvs_handle_t h;
    esp_err_t err = open_ns(id, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;  // ESP_ERR_NVS_NOT_FOUND when never provisioned
    }
    uint8_t enabled = 0;
    err = nvs_get_u8(h, K_ENABLED, &enabled);
    if (err != ESP_OK) {
        nvs_close(h);
        return ESP_ERR_NVS_NOT_FOUND;
    }
    uint8_t mode = 0;
    nvs_get_u8(h, K_MODE, &mode);
    out->enabled = enabled != 0;
    out->auth_mode = (secret_auth_mode_t)mode;
    size_t len = sizeof(out->secret);
    nvs_get_str(h, K_SECRET, out->secret, &len);
    len = sizeof(out->relay_url);
    nvs_get_str(h, K_RELAY_URL, out->relay_url, &len);
    len = sizeof(out->relay_token);
    nvs_get_str(h, K_RELAY_TOK, out->relay_token, &len);
    nvs_close(h);
    return ESP_OK;
}

esp_err_t secret_store_save_provider(const char *id, const provider_secret_t *cfg) {
    if (id == NULL || cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = open_ns(id, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(h, K_ENABLED, cfg->enabled ? 1 : 0);
    if (err == ESP_OK) err = nvs_set_u8(h, K_MODE, (uint8_t)cfg->auth_mode);
    if (err == ESP_OK) err = nvs_set_str(h, K_SECRET, cfg->secret);
    if (err == ESP_OK) err = nvs_set_str(h, K_RELAY_URL, cfg->relay_url);
    if (err == ESP_OK) err = nvs_set_str(h, K_RELAY_TOK, cfg->relay_token);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t secret_store_set_enabled(const char *id, bool enabled) {
    if (id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = open_ns(id, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(h, K_ENABLED, enabled ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t secret_store_erase_provider(const char *id) {
    if (id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = open_ns(id, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
    }
    err = nvs_erase_all(h);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

bool secret_store_provider_configured(const char *id) {
    if (id == NULL) {
        return false;
    }
    nvs_handle_t h;
    if (open_ns(id, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    uint8_t enabled;
    esp_err_t err = nvs_get_u8(h, K_ENABLED, &enabled);
    nvs_close(h);
    return err == ESP_OK;
}

bool secret_store_provider_enabled(const char *id) {
    provider_secret_t s;
    if (secret_store_load_provider(id, &s) != ESP_OK) {
        return false;
    }
    return s.enabled;
}

esp_err_t secret_store_save_wifi(const char *ssid, const char *pass) {
    nvs_handle_t h;
    esp_err_t err = open_ns(WIFI_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(h, "ssid", ssid != NULL ? ssid : "");
    if (err == ESP_OK) err = nvs_set_str(h, "pass", pass != NULL ? pass : "");
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t secret_store_load_wifi(char *ssid, size_t ssid_cap, char *pass, size_t pass_cap) {
    if (ssid == NULL || pass == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = open_ns(WIFI_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }
    size_t len = ssid_cap;
    err = nvs_get_str(h, "ssid", ssid, &len);
    if (err == ESP_OK) {
        len = pass_cap;
        if (nvs_get_str(h, "pass", pass, &len) != ESP_OK && pass_cap > 0) {
            pass[0] = '\0';
        }
    }
    nvs_close(h);
    return err;
}

bool secret_store_has_wifi(void) {
    char ssid[SECRET_STORE_SSID_MAXLEN];
    char pass[SECRET_STORE_PASS_MAXLEN];
    return secret_store_load_wifi(ssid, sizeof(ssid), pass, sizeof(pass)) == ESP_OK &&
           ssid[0] != '\0';
}

esp_err_t secret_store_erase_wifi(void) {
    nvs_handle_t h;
    esp_err_t err = open_ns(WIFI_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
    }
    err = nvs_erase_all(h);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t secret_store_factory_reset(void) {
    // Drop handles, erase the whole partition, then bring it back up empty.
    nvs_flash_deinit_partition(SECRET_PARTITION);  // ignore "not initialized"
    esp_err_t err = nvs_flash_erase_partition(SECRET_PARTITION);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "factory reset erase failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGW(TAG, "secret partition wiped (factory reset)");
    return secret_store_init();
}

void secret_store_redact(const char *secret, char *buf, size_t cap) {
    if (buf == NULL || cap == 0) {
        return;
    }
    if (secret == NULL || secret[0] == '\0') {
        snprintf(buf, cap, "(none)");
        return;
    }
    size_t n = strlen(secret);
    if (n <= 4) {
        snprintf(buf, cap, "****");
    } else {
        snprintf(buf, cap, "****%s", secret + n - 4);
    }
}
