#include "http_client.h"

#include <stdio.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "quota_types.h"

static const char *TAG = "http_client";

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
    bool truncated;
} body_ctx_t;

// Append response chunks into the caller buffer, bounded and NUL-terminated.
static esp_err_t on_event(esp_http_client_event_t *evt) {
    if (evt->event_id != HTTP_EVENT_ON_DATA) {
        return ESP_OK;
    }
    body_ctx_t *c = (body_ctx_t *)evt->user_data;
    if (c == NULL || c->buf == NULL || c->cap == 0) {
        return ESP_OK;
    }
    size_t space = (c->len < c->cap - 1) ? (c->cap - 1 - c->len) : 0;
    size_t n = ((size_t)evt->data_len < space) ? (size_t)evt->data_len : space;
    if (n > 0) {
        memcpy(c->buf + c->len, evt->data, n);
        c->len += n;
        c->buf[c->len] = '\0';
    }
    if ((size_t)evt->data_len > space) {
        c->truncated = true;
    }
    return ESP_OK;
}

static int classify_status(int http_status) {
    if (http_status >= 200 && http_status < 300) return QUOTA_STATUS_OK;
    if (http_status == 401) return QUOTA_STATUS_AUTH_FAILED;
    if (http_status == 403) return QUOTA_STATUS_NO_PERMISSION;
    if (http_status == 408) return QUOTA_STATUS_TIMEOUT;
    return QUOTA_STATUS_SERVICE_ERROR;  // other 4xx / 5xx
}

esp_err_t http_get(const http_get_req_t *req, http_get_resp_t *resp, int *quota_status) {
    int sink;
    if (quota_status == NULL) {
        quota_status = &sink;
    }
    if (req == NULL || req->url == NULL || resp == NULL || resp->body == NULL ||
        resp->body_cap == 0) {
        *quota_status = QUOTA_STATUS_SERVICE_ERROR;
        return ESP_ERR_INVALID_ARG;
    }
    resp->body[0] = '\0';
    resp->body_len = 0;
    resp->truncated = false;
    resp->http_status = 0;

    const int timeout = req->timeout_ms > 0 ? req->timeout_ms : HTTP_CLIENT_DEFAULT_TIMEOUT_MS;
    esp_err_t last_err = ESP_FAIL;

    // At most two attempts: original + one retry on transport failure (PRD §10.3).
    for (int attempt = 0; attempt < 2; attempt++) {
        body_ctx_t ctx = {.buf = resp->body, .cap = resp->body_cap, .len = 0, .truncated = false};
        ctx.buf[0] = '\0';

        esp_http_client_config_t cfg = {
            .url = req->url,
            .timeout_ms = timeout,
            .method = HTTP_METHOD_GET,
            .crt_bundle_attach = esp_crt_bundle_attach,  // TLS verification, always on
            .event_handler = on_event,
            .user_data = &ctx,
        };
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (client == NULL) {
            *quota_status = QUOTA_STATUS_SERVICE_ERROR;
            return ESP_FAIL;
        }
        esp_http_client_set_header(client, "Accept",
                                   req->accept != NULL ? req->accept : "application/json");

        char auth[768];
        auth[0] = '\0';
        if (req->bearer_token != NULL && req->bearer_token[0] != '\0') {
            int w = snprintf(auth, sizeof(auth), "Bearer %s", req->bearer_token);
            if (w > 0 && w < (int)sizeof(auth)) {
                esp_http_client_set_header(client, "Authorization", auth);
            }
        }

        esp_err_t err = esp_http_client_perform(client);
        int code = (err == ESP_OK) ? esp_http_client_get_status_code(client) : 0;

        // Scrub the bearer token from the stack before returning/looping.
        memset(auth, 0, sizeof(auth));
        esp_http_client_cleanup(client);

        if (err == ESP_OK) {
            resp->http_status = code;
            resp->body_len = ctx.len;
            resp->truncated = ctx.truncated;
            *quota_status = classify_status(code);
            // Log only id-free, non-sensitive facts (PRD §12).
            ESP_LOGI(TAG, "GET -> HTTP %d (%u bytes%s)", code, (unsigned)ctx.len,
                     ctx.truncated ? ", truncated" : "");
            return ESP_OK;
        }

        last_err = err;
        *quota_status = (err == ESP_ERR_HTTP_EAGAIN) ? QUOTA_STATUS_TIMEOUT
                                                     : QUOTA_STATUS_OFFLINE;
        ESP_LOGW(TAG, "GET attempt %d failed: %s", attempt + 1, esp_err_to_name(err));
    }
    return last_err;
}
