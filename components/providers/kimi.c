// Kimi / Moonshot AI — account balance provider (PRD §11.5).
//
// Endpoint (official, fully documented): GET
// https://api.moonshot.cn/v1/users/me/balance with Authorization: Bearer <key>.
// Response: {"code":0,"scode":"0x0","status":true,
//            "data":{"available_balance":F,"cash_balance":F,"voucher_balance":F}}
// available_balance = cash + voucher (spendable); cash can be negative (arrears).
#include <math.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "http_client.h"
#include "provider_util.h"
#include "providers.h"
#include "secret_store.h"

static const char *TAG = "kimi";
#define KIMI_URL "https://api.moonshot.cn/v1/users/me/balance"

static esp_err_t kimi_validate(void) {
    provider_secret_t s;
    esp_err_t err = secret_store_load_provider("kimi", &s);
    bool ok = (err == ESP_OK) && (s.auth_mode == SECRET_AUTH_DIRECT) && (s.secret[0] != '\0');
    memset(&s, 0, sizeof(s));
    if (err != ESP_OK) return ESP_ERR_NOT_FOUND;
    return ok ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static void parse_balance(const char *body, quota_result_t *r) {
    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        r->status = QUOTA_STATUS_API_CHANGED;
        return;
    }
    double code;
    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (provider_json_get_number(root, "code", &code) && (int)code != 0) {
        r->status = QUOTA_STATUS_SERVICE_ERROR;
        cJSON_Delete(root);
        return;
    }
    double available, cash, voucher;
    if (data == NULL || !provider_json_get_number(data, "available_balance", &available)) {
        r->status = QUOTA_STATUS_API_CHANGED;
        cJSON_Delete(root);
        return;
    }
    r->remaining = (float)available;  // primary card metric
    strncpy(r->unit, "CNY", sizeof(r->unit) - 1);
    if (provider_json_get_number(data, "cash_balance", &cash)) {
        r->aux1 = (float)cash;
        strncpy(r->aux1_label, "CASH", sizeof(r->aux1_label) - 1);
    }
    if (provider_json_get_number(data, "voucher_balance", &voucher)) {
        r->aux2 = (float)voucher;
        strncpy(r->aux2_label, "VOUCHER", sizeof(r->aux2_label) - 1);
    }
    r->updated_at = provider_now_unix();
    r->status = QUOTA_STATUS_OK;
    cJSON_Delete(root);
}

static esp_err_t kimi_fetch(quota_result_t *r) {
    quota_result_init(r, "kimi");
    strncpy(r->title, "Kimi", sizeof(r->title) - 1);

    provider_secret_t s;
    esp_err_t err = secret_store_load_provider("kimi", &s);
    if (err != ESP_OK || s.secret[0] == '\0') {
        memset(&s, 0, sizeof(s));
        r->status = QUOTA_STATUS_SETUP_REQUIRED;
        return ESP_OK;
    }

    char body[1024];
    http_get_resp_t resp = {.body = body, .body_cap = sizeof(body)};
    http_get_req_t req = {.url = KIMI_URL, .bearer_token = s.secret, .accept = "application/json"};
    int http_status = QUOTA_STATUS_SERVICE_ERROR;
    err = http_get(&req, &resp, &http_status);
    memset(&s, 0, sizeof(s));

    if (err != ESP_OK || http_status != QUOTA_STATUS_OK) {
        r->status = http_status;
        return ESP_OK;
    }
    parse_balance(body, r);
    ESP_LOGI(TAG, "kimi -> status %d (avail %.2f)", r->status,
             isnan(r->remaining) ? 0.0 : (double)r->remaining);
    return ESP_OK;
}

const quota_provider_t kimi_provider = {
    .id = "kimi",
    .display_name = "Kimi",
    .region = NULL,
    .metric_type = QUOTA_METRIC_BALANCE,
    .disclaimer = "API BALANCE",
    .fetch_usage = kimi_fetch,
    .validate_config = kimi_validate,
};
