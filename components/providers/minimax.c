// MiniMax CN / Global — Token Plan remaining-quota providers (PRD §11.1).
//
// Endpoint (official): GET https://<host>/v1/token_plan/remains with
// Authorization: Bearer <Subscription Key>. MiniMax doesn't publish the response
// schema; the parser below matches a real captured response: base_resp.status_code
// (0 = ok) plus model_remains[] where each model has
// current_interval_remaining_percent / current_weekly_remaining_percent and
// end_time / weekly_end_time (unix ms). It degrades to API_CHANGED if the shape
// changes rather than guessing.
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

    cJSON *mr = cJSON_GetObjectItemCaseSensitive(root, "model_remains");
    if (!cJSON_IsArray(mr) || cJSON_GetArraySize(mr) == 0) {
        r->status = QUOTA_STATUS_API_CHANGED;
        cJSON_Delete(root);
        return;
    }
    // Prefer the "general" (text) model entry, else the first one.
    cJSON *ent = NULL, *it = NULL;
    cJSON_ArrayForEach(it, mr) {
        const char *name = provider_json_get_string(it, "model_name");
        if (name != NULL && strcmp(name, "general") == 0) {
            ent = it;
            break;
        }
    }
    if (ent == NULL) ent = cJSON_GetArrayItem(mr, 0);

    // Token Plan reports a remaining-percent per window: a short "interval" cap
    // and a "weekly" cap. The headline is the binding (smaller) one.
    double interval_rem = NAN, weekly_rem = NAN, end_time = NAN, weekly_end = NAN;
    provider_json_get_number(ent, "current_interval_remaining_percent", &interval_rem);
    provider_json_get_number(ent, "current_weekly_remaining_percent", &weekly_rem);
    if (isnan(interval_rem) && isnan(weekly_rem)) {
        r->status = QUOTA_STATUS_API_CHANGED;
        cJSON_Delete(root);
        return;
    }
    double remaining;
    bool weekly_binds;
    if (!isnan(interval_rem) && !isnan(weekly_rem)) {
        weekly_binds = weekly_rem <= interval_rem;
        remaining = weekly_binds ? weekly_rem : interval_rem;
    } else if (!isnan(weekly_rem)) {
        remaining = weekly_rem;
        weekly_binds = true;
    } else {
        remaining = interval_rem;
        weekly_binds = false;
    }
    double used_pct = 100.0 - remaining;
    if (used_pct < 0) used_pct = 0;
    if (used_pct > 100) used_pct = 100;
    r->percentage = (float)used_pct;
    r->has_limit = false;
    r->unit[0] = '\0';  // percent-based plan, no unit

    // Surface both windows on the detail page.
    if (!isnan(interval_rem)) {
        r->aux1 = (float)interval_rem;
        strncpy(r->aux1_label, "INTERVAL%", sizeof(r->aux1_label) - 1);
    }
    if (!isnan(weekly_rem)) {
        r->aux2 = (float)weekly_rem;
        strncpy(r->aux2_label, "WEEKLY%", sizeof(r->aux2_label) - 1);
    }

    // Reset window of the binding cap (timestamps are unix milliseconds).
    provider_json_get_number(ent, "end_time", &end_time);
    provider_json_get_number(ent, "weekly_end_time", &weekly_end);
    double reset_ms = weekly_binds ? weekly_end : end_time;
    if (!isnan(reset_ms) && reset_ms > 0) {
        r->reset_at = (int64_t)(reset_ms / 1000.0);
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
    char body[4096];
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
