// secret_store.h — credential & Wi-Fi storage in a dedicated NVS partition.
//
// Everything sensitive lives in the `secret` partition (see partitions.csv), kept
// apart from the general `nvs` partition so factory-reset can wipe it wholesale.
// At rest, confidentiality is provided by Flash Encryption in production builds;
// when CONFIG_NVS_ENCRYPTION is enabled the partition is additionally opened with
// NVS encryption. See docs/SECURITY.md. Secrets are never logged in full — use
// secret_store_redact() for any diagnostic output (PRD §6.1).
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SECRET_STORE_SECRET_MAXLEN 320  // API keys / relay tokens can be long
#define SECRET_STORE_URL_MAXLEN 200     // relay base URL
#define SECRET_STORE_SSID_MAXLEN 33     // 32 + NUL
#define SECRET_STORE_PASS_MAXLEN 65     // 64 + NUL

typedef enum {
    SECRET_AUTH_DIRECT = 0,  // device-direct: `secret` holds the provider key
    SECRET_AUTH_RELAY = 1,   // relay: `relay_url` + `relay_token`
} secret_auth_mode_t;

typedef struct {
    bool enabled;
    secret_auth_mode_t auth_mode;
    char secret[SECRET_STORE_SECRET_MAXLEN];       // direct-mode credential
    char relay_url[SECRET_STORE_URL_MAXLEN];       // relay-mode base URL
    char relay_token[SECRET_STORE_SECRET_MAXLEN];  // relay-mode device token
} provider_secret_t;

// Initialise the secret partition (call once at boot, after nvs_flash_init for
// the default partition). Erases & recreates the partition if it is corrupt or
// from an incompatible NVS version.
esp_err_t secret_store_init(void);

// Load a provider's config. Returns ESP_ERR_NVS_NOT_FOUND if never provisioned.
esp_err_t secret_store_load_provider(const char *id, provider_secret_t *out);
// Persist a provider's full config.
esp_err_t secret_store_save_provider(const char *id, const provider_secret_t *cfg);
// Toggle only the enabled flag (provider management screen).
esp_err_t secret_store_set_enabled(const char *id, bool enabled);
// Erase one provider's stored config (D long-press / remove-provider).
esp_err_t secret_store_erase_provider(const char *id);
// Whether a provider has any stored config.
bool secret_store_provider_configured(const char *id);
// Whether a provider is configured AND enabled.
bool secret_store_provider_enabled(const char *id);

// Wi-Fi credentials.
esp_err_t secret_store_load_wifi(char *ssid, size_t ssid_cap, char *pass, size_t pass_cap);
esp_err_t secret_store_save_wifi(const char *ssid, const char *pass);
esp_err_t secret_store_erase_wifi(void);
bool secret_store_has_wifi(void);

// Wipe the entire secret partition (Fn+Del / factory-reset) and re-init it.
esp_err_t secret_store_factory_reset(void);

// Render a redacted view of `secret` (only the last 4 chars visible, e.g.
// "****1a2b") into buf. Safe for logs and the UI (PRD §6.1).
void secret_store_redact(const char *secret, char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
