#include "quota_types.h"

#include <math.h>
#include <string.h>

void quota_result_init(quota_result_t *r, const char *provider_id) {
    if (r == NULL) {
        return;
    }
    memset(r, 0, sizeof(*r));
    if (provider_id != NULL) {
        strncpy(r->provider_id, provider_id, sizeof(r->provider_id) - 1);
    }
    // NAN marks "not provided"; the UI checks isnan() before rendering a metric.
    r->used = NAN;
    r->limit = NAN;
    r->remaining = NAN;
    r->percentage = NAN;
    r->has_limit = false;
    r->cached = false;
    r->reset_at = 0;
    r->updated_at = 0;
    r->status = QUOTA_STATUS_SETUP_REQUIRED;
}

const char *quota_status_message(int status) {
    switch (status) {
        case QUOTA_STATUS_OK:             return "OK";
        case QUOTA_STATUS_SETUP_REQUIRED: return "SETUP REQUIRED";
        case QUOTA_STATUS_AUTH_FAILED:    return "AUTH FAILED";
        case QUOTA_STATUS_NO_PERMISSION:  return "NO PERMISSION";
        case QUOTA_STATUS_SERVICE_ERROR:  return "SERVICE ERROR";
        case QUOTA_STATUS_OFFLINE:        return "OFFLINE";
        case QUOTA_STATUS_TIMEOUT:        return "TIMEOUT";
        case QUOTA_STATUS_API_CHANGED:    return "API CHANGED";
        case QUOTA_STATUS_STALE:          return "STALE";
        default:                          return "UNKNOWN";
    }
}

const char *quota_status_badge(int status) {
    switch (status) {
        case QUOTA_STATUS_OK:             return "OK";
        case QUOTA_STATUS_SETUP_REQUIRED: return "SETUP";
        case QUOTA_STATUS_STALE:          return "STALE";
        default:                          return "ERR";  // every error state -> red ERR (PRD §8.4)
    }
}
