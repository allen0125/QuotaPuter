#include "refresh.h"

#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#include "provider_registry.h"
#include "result_cache.h"
#include "secret_store.h"
#include "wifi_manager.h"

namespace refresh {
namespace {

constexpr char TAG[] = "refresh";
constexpr int MAX_SLOTS = PROVIDER_REGISTRY_MAX;

struct Slot {
    Entry e;
    int64_t next_refresh;  // unix seconds
    int fail_count;
    bool force;
};

Slot s_slots[MAX_SLOTS];
int s_count;
SemaphoreHandle_t s_mtx;
int s_interval_min = 5;

int64_t now_s() { return (int64_t)time(NULL); }
void lock() { xSemaphoreTake(s_mtx, portMAX_DELAY); }
void unlock() { xSemaphoreGive(s_mtx); }

void load_interval() {
    nvs_handle_t h;
    if (nvs_open("app", NVS_READONLY, &h) == ESP_OK) {
        uint8_t v;
        if (nvs_get_u8(h, "refresh_min", &v) == ESP_OK &&
            (v == 1 || v == 5 || v == 15 || v == 30)) {
            s_interval_min = v;
        }
        nvs_close(h);
    }
}

void save_interval() {
    nvs_handle_t h;
    if (nvs_open("app", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "refresh_min", (uint8_t)s_interval_min);
        nvs_commit(h);
        nvs_close(h);
    }
}

// Apply a fetch outcome to a slot. Caller must hold the mutex.
void apply_locked(Slot *sl, const quota_result_t *res) {
    sl->e.last_status = res->status;
    if (res->status == QUOTA_STATUS_OK) {
        sl->e.result = *res;
        sl->e.have_data = true;
        sl->fail_count = 0;
        sl->next_refresh = now_s() + (int64_t)s_interval_min * 60;
    } else {
        if (sl->e.have_data) {
            // Keep the last good numbers, just mark them stale (PRD §5.3).
            sl->e.result.cached = true;
            sl->e.result.status = QUOTA_STATUS_STALE;
        } else {
            sl->e.result = *res;  // surface the error / setup state
        }
        if (sl->fail_count < 5) sl->fail_count++;
        int64_t backoff = (int64_t)s_interval_min * 60 * (1 << sl->fail_count);
        if (backoff > 1800) backoff = 1800;  // cap at 30 min
        sl->next_refresh = now_s() + backoff;
    }
}

void worker(void *arg) {
    (void)arg;
    for (;;) {
        int64_t now = now_s();
        bool wifi = wifi_manager_is_connected();
        for (int i = 0; i < s_count; i++) {
            Slot *sl = &s_slots[i];
            const char *id = sl->e.prov->id;
            bool enabled = secret_store_provider_enabled(id);

            lock();
            sl->e.configured = secret_store_provider_configured(id);
            bool forced = sl->force;
            bool due = forced || now >= sl->next_refresh;
            unlock();

            if (!enabled) continue;
            if (!wifi && !forced) continue;  // offline: keep cached data
            if (!due) continue;

            lock();
            sl->e.refreshing = true;
            sl->force = false;
            unlock();

            quota_result_t res;
            sl->e.prov->fetch_usage(&res);

            lock();
            apply_locked(sl, &res);
            sl->e.refreshing = false;
            unlock();

            if (res.status == QUOTA_STATUS_OK) {
                result_cache_save(&res);
            }
            vTaskDelay(pdMS_TO_TICKS(150));  // gentle spacing; never >1 in flight
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

int nth_enabled(int index) {
    int n = 0;
    for (int i = 0; i < s_count; i++) {
        if (secret_store_provider_enabled(s_slots[i].e.prov->id)) {
            if (n == index) return i;
            n++;
        }
    }
    return -1;
}

}  // namespace

void init() {
    s_mtx = xSemaphoreCreateMutex();
    load_interval();
    s_count = (int)provider_registry_count();
    if (s_count > MAX_SLOTS) s_count = MAX_SLOTS;

    for (int i = 0; i < s_count; i++) {
        Slot *sl = &s_slots[i];
        const quota_provider_t *p = provider_registry_get(i);
        memset(sl, 0, sizeof(*sl));
        sl->e.prov = p;
        quota_result_init(&sl->e.result, p->id);
        strncpy(sl->e.result.title, p->display_name, sizeof(sl->e.result.title) - 1);
        sl->e.configured = secret_store_provider_configured(p->id);
        sl->e.last_status = QUOTA_STATUS_SETUP_REQUIRED;
        sl->next_refresh = 0;  // refresh as soon as online

        quota_result_t cached;
        if (result_cache_load(p->id, &cached) == ESP_OK) {
            sl->e.result = cached;
            sl->e.result.status = QUOTA_STATUS_STALE;  // old until refreshed
            sl->e.have_data = true;
        }
    }
    xTaskCreate(worker, "refresh", 6144, NULL, 4, NULL);
    ESP_LOGI(TAG, "refresh manager started (%d providers, %d min interval)", s_count,
             s_interval_min);
}

int visible_count() {
    int n = 0;
    for (int i = 0; i < s_count; i++) {
        if (secret_store_provider_enabled(s_slots[i].e.prov->id)) n++;
    }
    return n;
}

bool get_visible(int index, Entry *out) {
    int i = nth_enabled(index);
    if (i < 0) return false;
    lock();
    *out = s_slots[i].e;
    unlock();
    return true;
}

bool get_by_id(const char *id, Entry *out) {
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_slots[i].e.prov->id, id) == 0) {
            lock();
            *out = s_slots[i].e;
            unlock();
            return true;
        }
    }
    return false;
}

void request(const char *id) {
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_slots[i].e.prov->id, id) == 0) {
            lock();
            s_slots[i].force = true;
            s_slots[i].next_refresh = 0;
            unlock();
            return;
        }
    }
}

void request_all() {
    lock();
    for (int i = 0; i < s_count; i++) {
        s_slots[i].force = true;
        s_slots[i].next_refresh = 0;
    }
    unlock();
}

int interval_minutes() { return s_interval_min; }

void set_interval_minutes(int minutes) {
    if (minutes == 1 || minutes == 5 || minutes == 15 || minutes == 30) {
        s_interval_min = minutes;
        save_interval();
        request_all();
    }
}

}  // namespace refresh
