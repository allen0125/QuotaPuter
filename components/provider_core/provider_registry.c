#include "provider_registry.h"

#include <string.h>

static const quota_provider_t *s_providers[PROVIDER_REGISTRY_MAX];
static size_t s_count;

esp_err_t provider_registry_register(const quota_provider_t *p) {
    if (p == NULL || p->id == NULL || p->id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (provider_registry_find(p->id) != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_count >= PROVIDER_REGISTRY_MAX) {
        return ESP_ERR_NO_MEM;
    }
    s_providers[s_count++] = p;
    return ESP_OK;
}

size_t provider_registry_count(void) {
    return s_count;
}

const quota_provider_t *provider_registry_get(size_t index) {
    return index < s_count ? s_providers[index] : NULL;
}

const quota_provider_t *provider_registry_find(const char *id) {
    if (id == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < s_count; i++) {
        if (strcmp(s_providers[i]->id, id) == 0) {
            return s_providers[i];
        }
    }
    return NULL;
}

void provider_registry_clear(void) {
    s_count = 0;
    memset(s_providers, 0, sizeof(s_providers));
}
