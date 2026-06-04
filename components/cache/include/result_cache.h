// result_cache.h — last-successful quota result per provider (PRD §5.4).
//
// Stored as a blob in the general `nvs` partition (not the secret partition):
// a quota_result_t carries no keys or response headers, so it is safe to cache.
// On boot the app loads cached values so the UI can show data offline, marked
// STALE. Requires the default NVS partition to be initialised first.
#pragma once

#include "esp_err.h"
#include "quota_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Persist a successful result, keyed by r->provider_id. The stored copy has
// cached=false (it represents a real fetch); result_cache_load marks it cached.
esp_err_t result_cache_save(const quota_result_t *r);

// Load the cached result for a provider into *out (with cached=true). Returns
// ESP_ERR_NVS_NOT_FOUND if nothing cached, ESP_ERR_INVALID_SIZE if the stored
// blob no longer matches the struct layout (treated as a cache miss by callers).
esp_err_t result_cache_load(const char *provider_id, quota_result_t *out);

// Remove one provider's cached result.
esp_err_t result_cache_erase(const char *provider_id);

// Drop every cached result (part of factory reset).
esp_err_t result_cache_clear_all(void);

#ifdef __cplusplus
}
#endif
