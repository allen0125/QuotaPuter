// providers.h — built-in provider singletons and registration entry point.
#pragma once

#include "quota_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Register every built-in provider into the provider registry (call once at boot
// before driving the UI/refresh loop).
void providers_register_all(void);

// Direct-mode providers (credentials on device).
extern const quota_provider_t minimax_cn_provider;
extern const quota_provider_t minimax_global_provider;
extern const quota_provider_t kimi_provider;

// Relay-mode providers (device holds only a relay URL + read-only token).
extern const quota_provider_t openai_provider;
extern const quota_provider_t anthropic_provider;
extern const quota_provider_t gemini_provider;

#ifdef __cplusplus
}
#endif
