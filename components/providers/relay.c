// Relay-mode providers: OpenAI / Anthropic / Gemini (PRD §6.2, §11.2-11.4).
//
// High-privilege credentials never touch the device. The user runs a small relay
// that talks to the official org/usage APIs and returns a standardized record:
//   {provider, metric_type, title, used, limit, unit, percentage, reset_at,
//    updated_at, status}
// The Cardputer stores only the relay URL and a read-only device token, GETs the
// relay, and renders the result with a disclaimer that this is org/API usage —
// not a personal ChatGPT/Claude/Gemini subscription.
#include <math.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "http_client.h"
#include "provider_util.h"
#include "providers.h"
#include "secret_store.h"

static const char *TAG = "relay";

static int map_relay_status(const char *st) {
    if (st == NULL || strcmp(st, "ok") == 0) return QUOTA_STATUS_OK;
    if (strcmp(st, "auth") == 0 || strcmp(st, "auth_failed") == 0 ||
        strcmp(st, "unauthorized") == 0)
        return QUOTA_STATUS_AUTH_FAILED;
    if (strcmp(st, "no_permission") == 0 || strcmp(st, "forbidden") == 0)
        return QUOTA_STATUS_NO_PERMISSION;
    if (strcmp(st, "stale") == 0) return QUOTA_STATUS_STALE;
    if (strcmp(st, "offline") == 0) return QUOTA_STATUS_OFFLINE;
    return QUOTA_STATUS_SERVICE_ERROR;
}

static esp_err_t relay_validate(const char *id) {
    provider_secret_t s;
    esp_err_t err = secret_store_load_provider(id, &s);
    bool ok = (err == ESP_OK) && (s.auth_mode == SECRET_AUTH_RELAY) && (s.relay_url[0] != '\0');
    memset(&s, 0, sizeof(s));
    if (err != ESP_OK) return ESP_ERR_NOT_FOUND;
    return ok ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static void parse_relay(const char *body, quota_result_t *r) {
    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        r->status = QUOTA_STATUS_API_CHANGED;
        return;
    }
    const char *title = provider_json_get_string(root, "title");
    if (title != NULL) strncpy(r->title, title, sizeof(r->title) - 1);
    const char *unit = provider_json_get_string(root, "unit");
    if (unit != NULL) strncpy(r->unit, unit, sizeof(r->unit) - 1);

    double v;
    if (provider_json_get_number(root, "used", &v)) r->used = (float)v;
    if (provider_json_get_number(root, "limit", &v)) {
        r->limit = (float)v;
        r->has_limit = true;
    }
    if (provider_json_get_number(root, "percentage", &v)) {
        r->percentage = (float)v;
    } else if (r->has_limit && !isnan(r->used) && r->limit > 0) {
        r->percentage = (float)(r->used / r->limit * 100.0);
    }
    if (r->has_limit && !isnan(r->used)) {
        r->remaining = (float)(r->limit - r->used);
    }
    const char *reset = provider_json_get_string(root, "reset_at");
    if (reset != NULL) {
        int64_t t = provider_iso8601_to_unix(reset);
        if (t > 0) r->reset_at = t;
    }
    const char *updated = provider_json_get_string(root, "updated_at");
    int64_t ut = updated != NULL ? provider_iso8601_to_unix(updated) : 0;
    r->updated_at = ut > 0 ? ut : provider_now_unix();

    r->status = map_relay_status(provider_json_get_string(root, "status"));
    cJSON_Delete(root);
}

static esp_err_t relay_fetch(const char *id, const char *def_title, quota_result_t *r) {
    quota_result_init(r, id);
    strncpy(r->title, def_title, sizeof(r->title) - 1);

    provider_secret_t s;
    esp_err_t err = secret_store_load_provider(id, &s);
    if (err != ESP_OK || s.relay_url[0] == '\0') {
        memset(&s, 0, sizeof(s));
        r->status = QUOTA_STATUS_SETUP_REQUIRED;
        return ESP_OK;
    }
    char body[2048];
    http_get_resp_t resp = {.body = body, .body_cap = sizeof(body)};
    http_get_req_t req = {
        .url = s.relay_url,
        .bearer_token = s.relay_token[0] != '\0' ? s.relay_token : NULL,
        .accept = "application/json",
    };
    int http_status = QUOTA_STATUS_SERVICE_ERROR;
    err = http_get(&req, &resp, &http_status);
    memset(&s, 0, sizeof(s));

    if (err != ESP_OK || http_status != QUOTA_STATUS_OK) {
        r->status = http_status;
        return ESP_OK;
    }
    parse_relay(body, r);
    ESP_LOGI(TAG, "%s -> status %d", id, r->status);
    return ESP_OK;
}

static esp_err_t openai_fetch(quota_result_t *r) { return relay_fetch("openai", "OpenAI API", r); }
static esp_err_t openai_validate(void) { return relay_validate("openai"); }
static esp_err_t anthropic_fetch(quota_result_t *r) {
    return relay_fetch("anthropic", "Anthropic", r);
}
static esp_err_t anthropic_validate(void) { return relay_validate("anthropic"); }
static esp_err_t gemini_fetch(quota_result_t *r) { return relay_fetch("gemini", "Gemini", r); }
static esp_err_t gemini_validate(void) { return relay_validate("gemini"); }

const quota_provider_t openai_provider = {
    .id = "openai",
    .display_name = "OpenAI",
    .region = NULL,
    .metric_type = QUOTA_METRIC_USAGE,
    .disclaimer = "API USAGE - NOT CHATGPT PLAN",
    .fetch_usage = openai_fetch,
    .validate_config = openai_validate,
};

const quota_provider_t anthropic_provider = {
    .id = "anthropic",
    .display_name = "Anthropic",
    .region = NULL,
    .metric_type = QUOTA_METRIC_USAGE,
    .disclaimer = "ORG USAGE - NOT CLAUDE PRO/MAX",
    .fetch_usage = anthropic_fetch,
    .validate_config = anthropic_validate,
};

const quota_provider_t gemini_provider = {
    .id = "gemini",
    .display_name = "Gemini",
    .region = NULL,
    .metric_type = QUOTA_METRIC_PROJECT,
    .disclaimer = "CLOUD PROJECT - NOT GOOGLE AI PLAN",
    .fetch_usage = gemini_fetch,
    .validate_config = gemini_validate,
};

// ChatGPT/Codex subscription PLAN usage (relay reads chatgpt.com wham/usage). This
// is the plan quota you see in the app — distinct from the `openai` API platform.
static esp_err_t codex_fetch(quota_result_t *r) { return relay_fetch("codex", "Codex", r); }
static esp_err_t codex_validate(void) { return relay_validate("codex"); }

const quota_provider_t codex_provider = {
    .id = "codex",
    .display_name = "Codex",
    .region = NULL,
    .metric_type = QUOTA_METRIC_USAGE,
    .disclaimer = "CHATGPT PLAN USAGE",
    .fetch_usage = codex_fetch,
    .validate_config = codex_validate,
};
