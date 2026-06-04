#include "provisioner.h"

#include <string.h>

#include "cJSON.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "provider_registry.h"
#include "providers.h"
#include "result_cache.h"
#include "secret_store.h"
#include "wifi_manager.h"

static const char *TAG = "provisioner";

#define QP_PREFIX "#QP "
#define QP_LINE_MAX 1400

// ---- response helpers -------------------------------------------------------

static void send_json(cJSON *resp) {
    char *str = cJSON_PrintUnformatted(resp);
    if (str != NULL) {
        usb_serial_jtag_write_bytes(QP_PREFIX, strlen(QP_PREFIX), pdMS_TO_TICKS(200));
        usb_serial_jtag_write_bytes(str, strlen(str), pdMS_TO_TICKS(500));
        usb_serial_jtag_write_bytes("\n", 1, pdMS_TO_TICKS(200));
        cJSON_free(str);
    }
    cJSON_Delete(resp);
}

static void send_error(const char *msg) {
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", false);
    cJSON_AddStringToObject(r, "error", msg);
    send_json(r);
}

static const char *str_field(const cJSON *cmd, const char *key, const char *dflt) {
    const cJSON *i = cJSON_GetObjectItemCaseSensitive(cmd, key);
    return cJSON_IsString(i) ? i->valuestring : dflt;
}

static bool bool_field(const cJSON *cmd, const char *key, bool dflt) {
    const cJSON *i = cJSON_GetObjectItemCaseSensitive(cmd, key);
    if (cJSON_IsBool(i)) return cJSON_IsTrue(i);
    return dflt;
}

// ---- command handlers -------------------------------------------------------

static void cmd_hello(void) {
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    cJSON_AddStringToObject(r, "device", "QuotaPuter");
    cJSON_AddStringToObject(r, "mode", "config");
    cJSON *ids = cJSON_AddArrayToObject(r, "providers");
    for (size_t i = 0; i < provider_registry_count(); i++) {
        cJSON_AddItemToArray(ids, cJSON_CreateString(provider_registry_get(i)->id));
    }
    send_json(r);
}

static void cmd_list(void) {
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    cJSON_AddBoolToObject(r, "wifi", secret_store_has_wifi());
    cJSON *arr = cJSON_AddArrayToObject(r, "providers");
    for (size_t i = 0; i < provider_registry_count(); i++) {
        const quota_provider_t *p = provider_registry_get(i);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", p->id);
        cJSON_AddStringToObject(o, "name", p->display_name);
        provider_secret_t s;
        bool configured = secret_store_load_provider(p->id, &s) == ESP_OK;
        cJSON_AddBoolToObject(o, "configured", configured);
        cJSON_AddBoolToObject(o, "enabled", configured && s.enabled);
        cJSON_AddStringToObject(o, "mode",
                                configured && s.auth_mode == SECRET_AUTH_RELAY ? "relay" : "direct");
        memset(&s, 0, sizeof(s));  // never expose secrets, scrub the copy
        cJSON_AddItemToArray(arr, o);
    }
    send_json(r);
}

static void cmd_set_wifi(const cJSON *cmd) {
    const char *ssid = str_field(cmd, "ssid", NULL);
    const char *pass = str_field(cmd, "password", "");
    if (ssid == NULL || ssid[0] == '\0') {
        send_error("ssid required");
        return;
    }
    esp_err_t err = secret_store_save_wifi(ssid, pass);
    if (err != ESP_OK) {
        send_error("save failed");
        return;
    }
    wifi_manager_connect(ssid, pass);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    cJSON_AddBoolToObject(r, "connecting", true);
    send_json(r);
    ESP_LOGI(TAG, "wifi credentials updated, connecting");
}

static void cmd_add_provider(const cJSON *cmd) {
    const char *id = str_field(cmd, "id", NULL);
    const quota_provider_t *p = id != NULL ? provider_registry_find(id) : NULL;
    if (p == NULL) {
        send_error("unknown provider id");
        return;
    }
    const char *mode = str_field(cmd, "mode", "direct");
    provider_secret_t s;
    memset(&s, 0, sizeof(s));
    s.enabled = bool_field(cmd, "enabled", true);
    if (strcmp(mode, "relay") == 0) {
        s.auth_mode = SECRET_AUTH_RELAY;
        const char *url = str_field(cmd, "relay_url", NULL);
        if (url == NULL || url[0] == '\0') {
            send_error("relay_url required");
            return;
        }
        strlcpy(s.relay_url, url, sizeof(s.relay_url));
        strlcpy(s.relay_token, str_field(cmd, "relay_token", ""), sizeof(s.relay_token));
    } else {
        s.auth_mode = SECRET_AUTH_DIRECT;
        const char *secret = str_field(cmd, "secret", NULL);
        if (secret == NULL || secret[0] == '\0') {
            send_error("secret required");
            return;
        }
        strlcpy(s.secret, secret, sizeof(s.secret));
    }
    esp_err_t err = secret_store_save_provider(id, &s);
    memset(&s, 0, sizeof(s));  // scrub plaintext copy
    if (err != ESP_OK) {
        send_error("save failed");
        return;
    }
    ESP_LOGI(TAG, "provider %s provisioned, running test fetch", id);

    // Immediate read-only test query (PRD §7.3 steps 5-6).
    quota_result_t res;
    p->fetch_usage(&res);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    cJSON_AddBoolToObject(r, "connected", res.status == QUOTA_STATUS_OK);
    cJSON_AddNumberToObject(r, "test_status", res.status);
    cJSON_AddStringToObject(r, "test_message", quota_status_message(res.status));
    send_json(r);
}

static void cmd_remove_provider(const cJSON *cmd) {
    const char *id = str_field(cmd, "id", NULL);
    if (id == NULL || provider_registry_find(id) == NULL) {
        send_error("unknown provider id");
        return;
    }
    secret_store_erase_provider(id);
    result_cache_erase(id);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    send_json(r);
    ESP_LOGI(TAG, "provider %s removed", id);
}

static void cmd_factory_reset(void) {
    secret_store_factory_reset();
    result_cache_clear_all();
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    send_json(r);
    ESP_LOGW(TAG, "factory reset performed");
}

static void handle_line(const char *line) {
    cJSON *cmd = cJSON_Parse(line);
    if (cmd == NULL) {
        send_error("invalid json");
        return;
    }
    const char *name = str_field(cmd, "cmd", "");
    if (strcmp(name, "hello") == 0) {
        cmd_hello();
    } else if (strcmp(name, "list") == 0) {
        cmd_list();
    } else if (strcmp(name, "set_wifi") == 0) {
        cmd_set_wifi(cmd);
    } else if (strcmp(name, "add_provider") == 0) {
        cmd_add_provider(cmd);
    } else if (strcmp(name, "remove_provider") == 0) {
        cmd_remove_provider(cmd);
    } else if (strcmp(name, "factory_reset") == 0) {
        cmd_factory_reset();
    } else {
        send_error("unknown command");
    }
    cJSON_Delete(cmd);
}

// ---- task -------------------------------------------------------------------

static void provisioner_task(void *arg) {
    (void)arg;
    static char line[QP_LINE_MAX];
    size_t len = 0;
    uint8_t rx[256];
    for (;;) {
        int n = usb_serial_jtag_read_bytes(rx, sizeof(rx), pdMS_TO_TICKS(100));
        for (int i = 0; i < n; i++) {
            char c = (char)rx[i];
            if (c == '\r') {
                continue;
            }
            if (c == '\n') {
                line[len] = '\0';
                if (len > 0) {
                    handle_line(line);
                }
                len = 0;
            } else if (len < sizeof(line) - 1) {
                line[len++] = c;
            } else {
                len = 0;  // overrun: drop the line
            }
        }
    }
}

esp_err_t provisioner_start(void) {
    usb_serial_jtag_driver_config_t cfg = {
        .tx_buffer_size = 1024,
        .rx_buffer_size = 1024,
    };
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        // INVALID_STATE means the console already installed it — that's fine.
        ESP_LOGE(TAG, "usb_serial_jtag install failed: %s", esp_err_to_name(err));
        return err;
    }
    if (xTaskCreate(provisioner_task, "provisioner", 6144, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "provisioning protocol ready on USB-Serial-JTAG");
    return ESP_OK;
}
