#include "wifi_manager.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "secret_store.h"

static const char *TAG = "wifi_manager";

#define BIT_CONNECTED BIT0

static bool s_inited;
static bool s_started;
static bool s_auto_reconnect;
static volatile wifi_state_t s_state = WIFI_STATE_IDLE;
static int s_fail_count;
static EventGroupHandle_t s_eg;
static esp_netif_t *s_netif;
static esp_timer_handle_t s_reconnect_timer;

// Backoff schedule: 0.5s, 1s, 2s, 4s, 8s, ... capped at 30s.
static uint64_t backoff_us(int fail_count) {
    uint32_t ms = 500u << (fail_count > 5 ? 5 : fail_count);
    if (ms > 30000u) ms = 30000u;
    return (uint64_t)ms * 1000u;
}

static void reconnect_cb(void *arg) {
    (void)arg;
    if (s_auto_reconnect) {
        esp_wifi_connect();
    }
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_eg, BIT_CONNECTED);
        if (!s_auto_reconnect) {
            s_state = WIFI_STATE_DISCONNECTED;
            return;
        }
        s_state = WIFI_STATE_CONNECTING;
        uint64_t delay = backoff_us(s_fail_count);
        if (s_fail_count < 30) s_fail_count++;
        esp_timer_stop(s_reconnect_timer);  // ignore "not running"
        esp_timer_start_once(s_reconnect_timer, delay);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_fail_count = 0;
        s_state = WIFI_STATE_CONNECTED;
        xEventGroupSetBits(s_eg, BIT_CONNECTED);
        ESP_LOGI(TAG, "connected, got IP");
    }
}

esp_err_t wifi_manager_init(void) {
    if (s_inited) {
        return ESP_OK;
    }
    s_eg = xEventGroupCreate();
    if (s_eg == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    s_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        on_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        on_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    const esp_timer_create_args_t targs = {.callback = reconnect_cb, .name = "wifi_reconnect"};
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_reconnect_timer));

    // SNTP makes "last updated" timestamps absolute (and correct across reboots)
    // once connectivity is available; it polls in the background until then.
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    s_inited = true;
    s_state = WIFI_STATE_IDLE;
    return ESP_OK;
}

esp_err_t wifi_manager_connect(const char *ssid, const char *pass) {
    if (!s_inited) {
        esp_err_t e = wifi_manager_init();
        if (e != ESP_OK) return e;
    }
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    wifi_config_t wc = {0};
    strlcpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
    if (pass != NULL) {
        strlcpy((char *)wc.sta.password, pass, sizeof(wc.sta.password));
    }
    // Permissive threshold (WPA and up) for broad home-router compatibility;
    // PMF capable so WPA3 networks also associate.
    wc.sta.threshold.authmode = (pass != NULL && pass[0] != '\0') ? WIFI_AUTH_WPA_PSK
                                                                  : WIFI_AUTH_OPEN;
    wc.sta.pmf_cfg.capable = true;
    wc.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (err != ESP_OK) {
        return err;
    }
    s_fail_count = 0;
    s_auto_reconnect = true;
    s_state = WIFI_STATE_CONNECTING;
    xEventGroupClearBits(s_eg, BIT_CONNECTED);

    if (!s_started) {
        err = esp_wifi_start();  // STA_START handler issues the first connect
        if (err == ESP_OK) s_started = true;
    } else {
        err = esp_wifi_connect();
    }
    return err;
}

esp_err_t wifi_manager_start_saved(void) {
    char ssid[SECRET_STORE_SSID_MAXLEN];
    char pass[SECRET_STORE_PASS_MAXLEN];
    esp_err_t err = secret_store_load_wifi(ssid, sizeof(ssid), pass, sizeof(pass));
    if (err != ESP_OK) {
        return err;
    }
    return wifi_manager_connect(ssid, pass);
}

void wifi_manager_disconnect(void) {
    s_auto_reconnect = false;
    if (s_reconnect_timer != NULL) {
        esp_timer_stop(s_reconnect_timer);
    }
    if (s_started) {
        esp_wifi_disconnect();
    }
    xEventGroupClearBits(s_eg, BIT_CONNECTED);
    s_state = WIFI_STATE_DISCONNECTED;
}

esp_err_t wifi_manager_clear(void) {
    wifi_manager_disconnect();
    return secret_store_erase_wifi();
}

wifi_state_t wifi_manager_state(void) {
    return s_state;
}

bool wifi_manager_is_connected(void) {
    return s_state == WIFI_STATE_CONNECTED &&
           (xEventGroupGetBits(s_eg) & BIT_CONNECTED) != 0;
}

bool wifi_manager_wait_connected(uint32_t timeout_ms) {
    if (s_eg == NULL) {
        return false;
    }
    EventBits_t bits = xEventGroupWaitBits(s_eg, BIT_CONNECTED, pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(timeout_ms));
    return (bits & BIT_CONNECTED) != 0;
}

int wifi_manager_rssi(void) {
    if (!wifi_manager_is_connected()) {
        return 0;
    }
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        return ap.rssi;
    }
    return 0;
}
