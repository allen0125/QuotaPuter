// http_client.h — HTTPS GET wrapper used by every provider (PRD §10.3).
//
// Always validates the server certificate against the bundled Mozilla root CAs
// (TLS verification can never be disabled), enforces a 10 s default timeout, and
// retries at most once on a transport failure. The Authorization header is set
// from a bearer token that is never logged.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HTTP_CLIENT_DEFAULT_TIMEOUT_MS 10000

typedef struct {
    const char *url;           // full https:// URL
    const char *bearer_token;  // optional; sent as "Authorization: Bearer <token>"
    const char *accept;        // optional Accept header (default "application/json")
    int timeout_ms;            // <= 0 selects HTTP_CLIENT_DEFAULT_TIMEOUT_MS
} http_get_req_t;

typedef struct {
    char *body;        // caller-owned buffer; NUL-terminated on return
    size_t body_cap;   // capacity of `body`
    size_t body_len;   // bytes written (excluding the NUL)
    bool truncated;    // true if the response exceeded body_cap
    int http_status;   // HTTP status code, or 0 if the request never completed
} http_get_resp_t;

// Perform an HTTPS GET. `resp->body`/`resp->body_cap` must be provided by the
// caller. *quota_status is always set to a quota_status_t value:
//   2xx -> OK, 401 -> AUTH_FAILED, 403 -> NO_PERMISSION, 408/read-timeout ->
//   TIMEOUT, other 4xx/5xx -> SERVICE_ERROR, connect/DNS failure -> OFFLINE.
// Returns ESP_OK when a response was received (even non-2xx); otherwise an
// esp_err_t describing the transport failure.
esp_err_t http_get(const http_get_req_t *req, http_get_resp_t *resp, int *quota_status);

#ifdef __cplusplus
}
#endif
