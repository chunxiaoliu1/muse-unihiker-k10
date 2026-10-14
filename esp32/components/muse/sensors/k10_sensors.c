#include "k10_sensors.h"
#include "k10_catalog.h"
#include "sensor_manager.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "k10.sensors";
static void append(char *out, size_t size, size_t *used, const char *format, ...)
    __attribute__((format(printf, 4, 5)));
static void append(char *out, size_t size, size_t *used, const char *format, ...)
{
    if (*used >= size) return;
    va_list args; va_start(args, format);
    int n = vsnprintf(out + *used, size - *used, format, args);
    va_end(args);
    if (n > 0) *used += (size_t)n < size - *used ? (size_t)n : size - *used - 1;
}

/* JSON and presentation enumerate the same registered channels. */
static cJSON *payload(const sensor_snapshot_t *snapshot, int64_t now)
{
    cJSON *out = cJSON_CreateObject();
    if (!out) return NULL;
    for (size_t i = 0; i < snapshot->count; ++i) {
        const sensor_reading_t *r = &snapshot->readings[i];
        const sensor_channel_t *channel = r->channel;
        if (!r->valid) {
            if (!cJSON_AddNullToObject(out, channel->key)) goto no_memory;
            continue;
        }
        cJSON *entry = cJSON_AddObjectToObject(out, channel->key);
        if (!entry) goto no_memory;
        if (channel->kind == SENSOR_VECTOR3) {
            cJSON *vector = cJSON_AddObjectToObject(entry, "value");
            if (!vector || !cJSON_AddNumberToObject(vector, "x", r->value.axis[0]) ||
                !cJSON_AddNumberToObject(vector, "y", r->value.axis[1]) ||
                !cJSON_AddNumberToObject(vector, "z", r->value.axis[2])) goto no_memory;
        } else if (!cJSON_AddNumberToObject(entry, "value", round(r->value.axis[0] * 100) / 100)) goto no_memory;
        if (!cJSON_AddStringToObject(entry, "unit", channel->unit) ||
            !cJSON_AddNumberToObject(entry, "age_s", (now - r->sampled_us) / 1000000.0)) goto no_memory;
        if (channel->estimated && !cJSON_AddBoolToObject(entry, "estimated", true)) goto no_memory;
    }
    return out;
no_memory:
    cJSON_Delete(out); return NULL;
}

cJSON *k10_sensors_command(void)
{
    sensor_snapshot_t snapshot; sensor_manager_snapshot(&snapshot);
    bool any = false;
    for (size_t i = 0; i < snapshot.count; ++i) any |= snapshot.readings[i].valid;
    cJSON *result = cJSON_CreateObject();
    if (!result) return NULL;
    if (!any) {
        cJSON_AddBoolToObject(result, "ok", false);
        cJSON *error = cJSON_AddObjectToObject(result, "error");
        cJSON_AddStringToObject(error, "code", "no_readings");
        cJSON_AddStringToObject(error, "message", "K10 has no fresh sensor readings");
        return result;
    }
    cJSON *readings = payload(&snapshot, esp_timer_get_time());
    if (!readings) { cJSON_Delete(result); return NULL; }
    cJSON_AddBoolToObject(result, "ok", true); cJSON_AddItemToObject(result, "payload", readings);
    ESP_LOGI(TAG, "sensors.read: %u registered channels", (unsigned)snapshot.count);
    return result;
}

void k10_sensors_text(char *out, size_t size)
{
    if (!size) return;
    sensor_snapshot_t snapshot; sensor_manager_snapshot(&snapshot);
    size_t used = 0; out[0] = 0;
    for (size_t i = 0; i < snapshot.count; ++i) {
        const sensor_reading_t *r = &snapshot.readings[i];
        append(out, size, &used, "%s%s\n", i ? "\n" : "", r->channel->label);
        if (!r->valid) append(out, size, &used, "Unavailable");
        else if (r->channel->kind == SENSOR_VECTOR3)
            append(out, size, &used, "X %+.0f\nY %+.0f\nZ %+.0f", r->value.axis[0], r->value.axis[1], r->value.axis[2]);
        else append(out, size, &used, "%.1f %s", r->value.axis[0], r->channel->display_unit);
    }
    if (!snapshot.count) snprintf(out, size, "Unavailable");
}

void k10_sensors_print(void)
{
    sensor_snapshot_t snapshot; sensor_manager_snapshot(&snapshot);
    const sensor_reading_t *temp = sensor_snapshot_find(&snapshot, "temperature");
    const sensor_reading_t *rh = sensor_snapshot_find(&snapshot, "humidity");
    const sensor_reading_t *light = sensor_snapshot_find(&snapshot, "light");
    const sensor_reading_t *acc = sensor_snapshot_find(&snapshot, "acceleration");
    cJSON *root = cJSON_CreateObject(); if (!root) return;
    /* Keep the original serial keys; new sensors automatically appear in readings. */
    cJSON_AddBoolToObject(root, "climate_ok", temp && rh && temp->valid && rh->valid);
    cJSON_AddNumberToObject(root, "temperature_c", temp ? round(temp->value.axis[0]*100)/100 : 0);
    cJSON_AddNumberToObject(root, "humidity_rh", rh ? round(rh->value.axis[0]*100)/100 : 0);
    cJSON_AddBoolToObject(root, "light_ok", light && light->valid);
    cJSON_AddNumberToObject(root, "lux_estimate", light ? round(light->value.axis[0]*100)/100 : 0);
    cJSON_AddNumberToObject(root, "ch0", light ? light->value.raw[0] : 0);
    cJSON_AddNumberToObject(root, "ch1", light ? light->value.raw[1] : 0);
    cJSON_AddBoolToObject(root, "accel_ok", acc && acc->valid);
    cJSON *vector = cJSON_AddArrayToObject(root, "accel_raw12");
    for (int axis = 0; axis < 3; ++axis) cJSON_AddItemToArray(vector, cJSON_CreateNumber(acc ? acc->value.axis[axis] : 0));
    cJSON *all = payload(&snapshot, esp_timer_get_time());
    if (all) cJSON_AddItemToObject(root, "readings", all);
    char *json = cJSON_PrintUnformatted(root);
    if (json) { printf("@sensors %s\n", json); fflush(stdout); }
    free(json); cJSON_Delete(root);
}

const char *k10_sensors_description(void)
{
    EXT_RAM_BSS_ATTR static char description[SENSOR_DESCRIPTION_BYTES];
    if (description[0]) return description;
    size_t used = 0;
    append(description, sizeof(description), &used,
           "Read UNIHIKER K10 onboard sensors. Each field has value, unit and age_s; "
           "missing/failed readings or readings older than %lld seconds are null. "
           "Call for current measurements; do not invent readings. Channels: ", (long long)(SENSOR_STALE_US / 1000000));
    for (size_t i = 0; i < K10_SENSOR_DRIVER_COUNT; ++i)
        for (size_t j = 0; j < K10_SENSOR_DRIVERS[i]->channel_count; ++j) {
            const sensor_channel_t *channel = &K10_SENSOR_DRIVERS[i]->channels[j];
            append(description, sizeof(description), &used, "%s (%s%s). %s ", channel->key,
                   channel->unit, channel->estimated ? ", estimated" : "", channel->note ? channel->note : "");
        }
    return description;
}

static void on_snapshot(const sensor_snapshot_t *snapshot)
{
    static int64_t next_log;
    sensor_events_tick(snapshot);
    int64_t now = esp_timer_get_time();
    if (now >= next_log) { next_log = now + (int64_t)SENSOR_LOG_PERIOD_MS * 1000; k10_sensors_print(); }
}

void k10_sensors_start(i2c_master_bus_handle_t bus)
{
    if (!sensor_events_init()) ESP_LOGE(TAG, "Event initialization failed");
    if (!sensor_manager_start(bus, K10_SENSOR_DRIVERS, K10_SENSOR_DRIVER_COUNT, on_snapshot))
        ESP_LOGE(TAG, "Sensor manager initialization failed");
}
