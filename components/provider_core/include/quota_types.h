// quota_types.h — core data model shared by every QuotaPuter provider.
//
// Mirrors PRD §10.2. The data/network/provider layers are plain C so they can
// use esp_http_client / cJSON / nvs directly; the C++ UI layer includes this
// header too (the structs are C-ABI and C++-compatible).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Field capacities (match PRD §10.2 quota_result_t).
#define QUOTA_ID_MAXLEN 32
#define QUOTA_TITLE_MAXLEN 32
#define QUOTA_UNIT_MAXLEN 16

// metric_type values — drive how the detail screen lays out a provider
// (PRD §5.2 table). Stored as strings so a relay can supply them verbatim.
#define QUOTA_METRIC_SUBSCRIPTION "subscription"  // 订阅额度型: remaining %, used/total, reset window
#define QUOTA_METRIC_USAGE "usage"                // API 用量型: tokens / cost over a period
#define QUOTA_METRIC_BALANCE "balance"            // 余额型: available / cash / voucher
#define QUOTA_METRIC_PROJECT "project"            // 项目监控型: calls / tokens / quota consumed

// Request/result status (PRD §12 error matrix). The numeric value is stored in
// quota_result_t.status; helpers below map it to UI text.
typedef enum {
    QUOTA_STATUS_OK = 0,            // fresh, valid data
    QUOTA_STATUS_SETUP_REQUIRED,   // no credentials provisioned        -> "SETUP REQUIRED"
    QUOTA_STATUS_AUTH_FAILED,      // key invalid / expired             -> "AUTH FAILED"
    QUOTA_STATUS_NO_PERMISSION,    // key lacks permission              -> "NO PERMISSION"
    QUOTA_STATUS_SERVICE_ERROR,    // official API returned an error    -> "SERVICE ERROR"
    QUOTA_STATUS_OFFLINE,          // no network / DNS / connect fail   -> "OFFLINE"
    QUOTA_STATUS_TIMEOUT,          // request exceeded timeout          -> "TIMEOUT"
    QUOTA_STATUS_API_CHANGED,      // response could not be parsed      -> "API CHANGED"
    QUOTA_STATUS_STALE,            // showing cached data               -> "STALE"
    QUOTA_STATUS_MAX
} quota_status_t;

// Unified result record (PRD §10.2). Numeric metrics use NAN when not provided.
typedef struct {
    char provider_id[QUOTA_ID_MAXLEN];
    char title[QUOTA_TITLE_MAXLEN];
    char unit[QUOTA_UNIT_MAXLEN];
    float used;        // amount consumed (metric-dependent)
    float limit;       // total allowance, when has_limit
    float remaining;   // limit - used, when known
    float percentage;  // 0..100 used%, when known
    float aux1;        // optional secondary metric (e.g. Kimi cash balance)
    float aux2;        // optional secondary metric (e.g. Kimi voucher balance)
    char aux1_label[12];  // label for aux1 (e.g. "CASH"), empty if unused
    char aux2_label[12];  // label for aux2 (e.g. "VOUCHER"), empty if unused
    int64_t reset_at;  // unix seconds of next reset/window end, 0 if none
    int64_t updated_at;// unix seconds of this successful fetch, 0 if never
    bool has_limit;    // whether `limit` is meaningful
    bool cached;       // whether this value came from the offline cache
    int status;        // quota_status_t
} quota_result_t;

// Provider vtable (PRD §10.2). Implementations live in components/providers/*.
typedef struct {
    const char *id;            // stable id, e.g. "minimax_cn"
    const char *display_name;  // e.g. "MiniMax CN"
    const char *region;        // short badge, e.g. "CN" / "GL" / NULL
    const char *metric_type;   // one of QUOTA_METRIC_*
    const char *disclaimer;    // detail-page banner (PRD §11), may be NULL
    // Perform one read-only query and fill *result. Must set result->status and,
    // on success, result->updated_at. Returns ESP_OK on a successful fetch.
    esp_err_t (*fetch_usage)(quota_result_t *result);
    // Validate that the provider is configured (credentials present & well-formed)
    // without contacting the network. Returns ESP_OK if ready to fetch.
    esp_err_t (*validate_config)(void);
} quota_provider_t;

// Initialise a result to an empty/unknown state for the given provider id.
void quota_result_init(quota_result_t *r, const char *provider_id);

// Full status text per PRD §12 (e.g. "AUTH FAILED"). Never NULL.
const char *quota_status_message(int status);

// Short status badge per PRD §8.4 (one of "OK"/"ERR"/"STALE"/"SETUP"). Never NULL.
const char *quota_status_badge(int status);

#ifdef __cplusplus
}
#endif
