#include "result_cache.h"

#include "nvs.h"

#define CACHE_NS "qcache"

esp_err_t result_cache_save(const quota_result_t *r) {
    if (r == NULL || r->provider_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(CACHE_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    quota_result_t stored = *r;
    stored.cached = false;  // on disk it represents the real fetch
    err = nvs_set_blob(h, r->provider_id, &stored, sizeof(stored));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t result_cache_load(const char *provider_id, quota_result_t *out) {
    if (provider_id == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(CACHE_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }
    size_t len = sizeof(*out);
    err = nvs_get_blob(h, provider_id, out, &len);
    nvs_close(h);
    if (err != ESP_OK) {
        return err;
    }
    if (len != sizeof(*out)) {
        return ESP_ERR_INVALID_SIZE;  // struct changed between firmware versions
    }
    out->cached = true;
    return ESP_OK;
}

esp_err_t result_cache_erase(const char *provider_id) {
    if (provider_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(CACHE_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
    }
    err = nvs_erase_key(h, provider_id);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t result_cache_clear_all(void) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(CACHE_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
    }
    err = nvs_erase_all(h);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
