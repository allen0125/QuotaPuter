#include "provider_util.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

int64_t provider_now_unix(void) {
    return (int64_t)time(NULL);
}

// Howard Hinnant's days-from-civil: days since 1970-01-01 for a Y/M/D (proleptic
// Gregorian). Avoids depending on timegm() availability.
static int64_t days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

int64_t provider_iso8601_to_unix(const char *s) {
    if (s == NULL) {
        return 0;
    }
    int Y, Mo, D, h = 0, mi = 0, se = 0;
    int n = sscanf(s, "%d-%d-%dT%d:%d:%d", &Y, &Mo, &D, &h, &mi, &se);
    if (n < 3) {
        return 0;
    }
    int64_t secs = days_from_civil(Y, (unsigned)Mo, (unsigned)D) * 86400 +
                   (int64_t)h * 3600 + (int64_t)mi * 60 + se;

    // Optional timezone offset after the 'T' (the time part has no '-'/'+', so
    // the first such char is the zone sign). Absent or 'Z' means UTC.
    const char *t = strchr(s, 'T');
    if (t != NULL) {
        const char *plus = strchr(t, '+');
        const char *minus = strchr(t, '-');
        const char *off = plus != NULL ? plus : minus;
        if (off != NULL) {
            int oh = 0, om = 0;
            if (sscanf(off + 1, "%d:%d", &oh, &om) >= 1) {
                int sign = (*off == '+') ? 1 : -1;
                secs -= sign * ((int64_t)oh * 3600 + (int64_t)om * 60);
            }
        }
    }
    return secs;
}

bool provider_json_get_number(const cJSON *obj, const char *key, double *out) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(item)) {
        if (out != NULL) {
            *out = item->valuedouble;
        }
        return true;
    }
    return false;
}

const char *provider_json_get_string(const cJSON *obj, const char *key) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}
