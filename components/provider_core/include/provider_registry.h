// provider_registry.h — runtime registry of available providers.
//
// Providers register themselves (via providers_register_all() in the providers
// component) rather than being referenced by symbol here, so provider_core stays
// at the bottom of the dependency graph and links independently of any concrete
// provider. The app iterates the registry to drive refresh and the UI.
#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "quota_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Upper bound on registered providers (PRD requires 6: minimax_cn/global,
// openai, anthropic, gemini, kimi). A little headroom for future additions.
#define PROVIDER_REGISTRY_MAX 8

// Register a provider. The pointer must remain valid for the program lifetime
// (providers are static singletons). Returns:
//   ESP_OK                 on success
//   ESP_ERR_INVALID_ARG    p or p->id is NULL/empty
//   ESP_ERR_INVALID_STATE  a provider with the same id is already registered
//   ESP_ERR_NO_MEM         registry is full
esp_err_t provider_registry_register(const quota_provider_t *p);

// Count of registered providers.
size_t provider_registry_count(void);

// i-th provider in registration order, or NULL if index is out of range.
const quota_provider_t *provider_registry_get(size_t index);

// Find a registered provider by id, or NULL.
const quota_provider_t *provider_registry_find(const char *id);

// Drop all registrations (test/reset aid).
void provider_registry_clear(void);

#ifdef __cplusplus
}
#endif
