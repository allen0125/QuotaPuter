// refresh.h — quota refresh scheduler & cache (PRD §5.3, §5.4, §10.3).
//
// Owns one entry per registered provider, loads cached results on boot, and runs
// a worker task that sequentially (so never >2 concurrent, PRD §10.3) refreshes
// the enabled providers on the configured interval, with exponential backoff on
// failure and STALE fallback to the last good data. The UI reads snapshots under
// a mutex; the worker never holds the mutex across a network call.
#pragma once

#include <stdint.h>

#include "quota_types.h"

namespace refresh {

struct Entry {
    const quota_provider_t *prov;
    quota_result_t result;  // best-known data to display
    bool configured;        // credentials present
    bool have_data;         // ever fetched/cached real numbers
    bool refreshing;        // a fetch is in flight
    int last_status;        // most recent fetch outcome (quota_status_t)
};

// Build entries from the registry, load cached results, and start the worker.
void init();

// Number of currently enabled providers (the overview shows exactly these).
int visible_count();

// Snapshot the i-th enabled provider (0-based) into *out. False if out of range.
bool get_visible(int index, Entry *out);

// Snapshot a provider by id. False if unknown.
bool get_by_id(const char *id, Entry *out);

// Force an immediate refresh of one provider / all providers (manual 'R').
void request(const char *id);
void request_all();

// Refresh interval in minutes (1/5/15/30), persisted in NVS. Default 5.
int interval_minutes();
void set_interval_minutes(int minutes);

}  // namespace refresh
