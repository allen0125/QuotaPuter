// provider_util.h — small helpers shared by provider implementations.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

// Current wall-clock time in unix seconds (boot-relative until SNTP syncs;
// relative "x ago" displays are correct regardless).
int64_t provider_now_unix(void);

// Parse an ISO-8601 timestamp ("YYYY-MM-DDThh:mm:ss[.fff][Z|+hh:mm]") to unix
// seconds (UTC). Returns 0 if it cannot be parsed.
int64_t provider_iso8601_to_unix(const char *s);

// Read a numeric field; returns false if missing, null, or not a number.
bool provider_json_get_number(const cJSON *obj, const char *key, double *out);

// Read a string field; returns NULL if missing/null/not a string.
const char *provider_json_get_string(const cJSON *obj, const char *key);

#ifdef __cplusplus
}
#endif
