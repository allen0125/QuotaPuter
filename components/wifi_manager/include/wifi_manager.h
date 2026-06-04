// wifi_manager.h — Wi-Fi station bring-up, status, and auto-reconnect (PRD §5.5).
//
// Connects in STA mode using credentials supplied directly or loaded from
// secret_store, and transparently reconnects with exponential backoff if the
// link drops. The UI polls wifi_manager_state()/is_connected() for the title-bar
// indicator and gates refresh on connectivity.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_STATE_IDLE = 0,      // no credentials / not started
    WIFI_STATE_CONNECTING,    // associating or retrying
    WIFI_STATE_CONNECTED,     // associated and has an IP
    WIFI_STATE_DISCONNECTED,  // intentionally stopped
} wifi_state_t;

// One-time driver/netif/event-loop init. Safe to call once at boot.
esp_err_t wifi_manager_init(void);

// Connect to the given network (does NOT persist; caller saves via secret_store).
// Enables auto-reconnect. ssid is required; pass may be NULL/empty for open APs.
esp_err_t wifi_manager_connect(const char *ssid, const char *pass);

// Load saved Wi-Fi credentials from secret_store and connect.
// Returns ESP_ERR_NVS_NOT_FOUND if no credentials are stored.
esp_err_t wifi_manager_start_saved(void);

// Stop the link and disable auto-reconnect.
void wifi_manager_disconnect(void);

// Disconnect and erase the saved credentials (PRD §5.5 clear Wi-Fi).
esp_err_t wifi_manager_clear(void);

wifi_state_t wifi_manager_state(void);
bool wifi_manager_is_connected(void);

// Block until connected or timeout elapses. Returns true if connected.
bool wifi_manager_wait_connected(uint32_t timeout_ms);

// Current AP RSSI in dBm, or 0 when not connected.
int wifi_manager_rssi(void);

#ifdef __cplusplus
}
#endif
