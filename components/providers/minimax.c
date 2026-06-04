// MiniMax CN / Global — Token Plan remaining-quota providers (PRD §11.1).
//
// Endpoint (official): GET https://<host>/v1/token_plan/remains with
// Authorization: Bearer <Subscription Key>. The response *schema* is not
// published by MiniMax, so the parser below is defensive: it reads the commonly
// observed shape (base_resp + model_remains[] with total/remain or a remaining
// usage_percent and an end_time window) and degrades to API_CHANGED when the
// payload doesn't match, rather than guessing. Adjust field names here once a
// real Subscription Key response is captured.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "http_client.h"
#include "provider_util.h"
#include "providers.h"
#include "secret_store.h"

static const char *TAG = "minimax";

static esp_err_t minimax_validate(const char *id) {
    provider_secret_t s;
    esp_err_t err = secret_store_load_provider(id, &s);
    bool ok = (err == ESP_OK) && (s.auth_mode == SECRET_AUTH_DIRECT) && (s.secret[0] != '\0');
    memset(&s, 0, sizeof(s));
    if (err != ESP_OK) return ESP_ERR_NOT_FOUND;
    return ok ? ESP_OK : ESP_ERR_INVALID_STATE;
}

// Map a MiniMax base_resp.status_code to our status. 0 = success.
static int map_base_status(int code) {
    switch (code) {
        case 0:    return QUOTA_STATUS_OK;
        case 1004: return QUOTA_STATUS_AUTH_FAILED;      // invalid/expired auth
        case 1008: return QUOTA_STATUS_AUTH_FAILED;      // insufficient balance / no access
        case 1039: return QUOTA_STATUS_SERVICE_ERROR;    // rate limited
        default:   return QUOTA_STATUS_SERVICE_ERROR;
    }
}

static void parse_token_plan(const char *body, quota_result_t *r) {
    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        r->status = QUOTA_STATUS_API_CHANGED;
        return;
    }
    cJSON *base = cJSON_GetObjectItemCaseSensitive(root, "base_resp");
    double code;
    if (base != NULL && provider_json_get_number(base, "status_code", &code) && (int)code != 0) {
        r->status = map_base_status((int)code);
        cJSON_Delete(root);
        return;
    }

    double total = NAN, remain = NAN, usage_pct = NAN, end_time = NAN;
    cJSON *mr = cJSON_GetObjectItemCaseSensitive(root, "model_remains");
    if (cJSON_IsArray(mr) && cJSON_GetArraySize(mr) > 0) {
        // Choose the entry with the largest 'total' (the main plan), else the first.
        cJSON *best = NULL, *it = NULL;
        double best_total = -1.0;
        cJSON_ArrayForEach(it, mr) {
            double t;
            if (provider_json_get_number(it, "total", &t) && t > best_total) {
                best_total = t;
                best = it;
            }
        }
        cJSON *ent = best != NULL ? best : cJSON_GetArrayItem(mr, 0);
        double v;
        if (provider_json_get_number(ent, "total", &v)) total = v;
        if (provider_json_get_number(ent, "remain", &v)) remain = v;
        else if (provider_json_get_number(ent, "remains", &v)) remain = v;
        if (provider_json_get_number(ent, "usage_percent", &v)) usage_pct = v;
        if (provider_json_get_number(ent, "end_time", &v)) end_time = v;
    }
    if (isnan(total) && isnan(remain) && isnan(usage_pct)) {
        // Fall back to a couple of plausible top-level field names.
        double v;
        if (provider_json_get_number(root, "total_quota", &v)) total = v;
        if (provider_json_get_number(root, "remain_quota", &v)) remain = v;
    }
    if (isnan(total) && isnan(remain) && isnan(usage_pct)) {
        r->status = QUOTA_STATUS_API_CHANGED;
        cJSON_Delete(root);
        return;
    }

    if (!isnan(total) && total > 0 && !isnan(remain)) {
        r->limit = (float)total;
        r->remaining = (float)remain;
        r->used = (float)(total - remain);
        r->has_limit = true;
        r->percentage = (float)((total - remain) / total * 100.0);
    } else if (!isnan(usage_pct)) {
        // Observed: usage_percent is the REMAINING fraction, so used% = 100 - it.
        double used_pct = 100.0 - usage_pct;
        if (used_pct < 0) used_pct = 0;
        if (used_pct > 100) used_pct = 100;
        r->percentage = (float)used_pct;
        if (!isnan(remain)) r->remaining = (float)remain;
    }
    strncpy(r->unit, "tokens", sizeof(r->unit) - 1);
    if (!isnan(end_time) && end_time > 0) {
        r->reset_at = (int64_t)end_time;
    }
    r->updated_at = provider_now_unix();
    r->status = QUOTA_STATUS_OK;
    cJSON_Delete(root);
}

static esp_err_t minimax_fetch(const char *id, const char *display, const char *host,
                               quota_result_t *r) {
    quota_result_init(r, id);
    strncpy(r->title, display, sizeof(r->title) - 1);

    provider_secret_t s;
    esp_err_t err = secret_store_load_provider(id, &s);
    if (err != ESP_OK || s.secret[0] == '\0') {
        memset(&s, 0, sizeof(s));
        r->status = QUOTA_STATUS_SETUP_REQUIRED;
        return ESP_OK;
    }

    char url[160];
    snprintf(url, sizeof(url), "https://%s/v1/token_plan/remains", host);
    char body[2048];
    http_get_resp_t resp = {.body = body, .body_cap = sizeof(body)};
    http_get_req_t req = {.url = url, .bearer_token = s.secret, .accept = "application/json"};
    int http_status = QUOTA_STATUS_SERVICE_ERROR;
    err = http_get(&req, &resp, &http_status);
    memset(&s, 0, sizeof(s));  // scrub key copy

    if (err != ESP_OK) {
        r->status = http_status;  // OFFLINE / TIMEOUT
        return ESP_OK;
    }
    if (http_status != QUOTA_STATUS_OK) {
        r->status = http_status;  // AUTH_FAILED / NO_PERMISSION / SERVICE_ERROR
        return ESP_OK;
    }
    parse_token_plan(body, r);
    ESP_LOGI(TAG, "%s -> status %d (%.0f%% used)", id, r->status,
             isnan(r->percentage) ? 0.0 : (double)r->percentage);
    return ESP_OK;
}

static esp_err_t cn_fetch(quota_result_t *r) {
    return minimax_fetch("minimax_cn", "MiniMax CN", "www.minimaxi.com", r);
}
static esp_err_t cn_validate(void) { return minimax_validate("minimax_cn"); }

static esp_err_t gl_fetch(quota_result_t *r) {
    return minimax_fetch("minimax_global", "MiniMax GL", "www.minimax.io", r);
}
static esp_err_t gl_validate(void) { return minimax_validate("minimax_global"); }

const quota_provider_t minimax_cn_provider = {
    .id = "minimax_cn",
    .display_name = "MiniMax CN",
    .region = "CN",
    .metric_type = QUOTA_METRIC_SUBSCRIPTION,
    .disclaimer = NULL,
    .fetch_usage = cn_fetch,
    .validate_config = cn_validate,
};

const quota_provider_t minimax_global_provider = {
    .id = "minimax_global",
    .display_name = "MiniMax Global",
    .region = "GL",
    .metric_type = QUOTA_METRIC_SUBSCRIPTION,
    .disclaimer = NULL,
    .fetch_usage = gl_fetch,
    .validate_config = gl_validate,
};
