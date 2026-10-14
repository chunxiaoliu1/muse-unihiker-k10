#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "sensor_config.h"

typedef enum { SENSOR_NUMBER, SENSOR_VECTOR3 } sensor_kind_t;
typedef struct {
    const char *key;             /* Stable Muse JSON field name. */
    const char *label;           /* Compact display label. */
    const char *unit;            /* Machine-readable unit. */
    const char *display_unit;
    sensor_kind_t kind;
    bool estimated;
    const char *note;            /* Optional advice in command registration. */
} sensor_channel_t;

typedef struct { double axis[3]; unsigned raw[2]; } sensor_value_t;
typedef struct {
    const sensor_channel_t *channel;
    sensor_value_t value;
    bool valid;
    int64_t sampled_us;
} sensor_reading_t;
typedef struct {
    size_t count;
    sensor_reading_t readings[SENSOR_MAX_READINGS];
} sensor_snapshot_t;

/* The catalog and snapshots retain static channel descriptors for their lifetime. */
static inline const sensor_reading_t *sensor_snapshot_find(const sensor_snapshot_t *snapshot, const char *key)
{
    for (size_t i = 0; i < snapshot->count; ++i)
        if (!strcmp(snapshot->readings[i].channel->key, key)) return &snapshot->readings[i];
    return NULL;
}

static inline void sensor_snapshot_expire(sensor_snapshot_t *snapshot, int64_t now_us)
{
    for (size_t i = 0; i < snapshot->count; ++i) {
        sensor_reading_t *r = &snapshot->readings[i];
        if (now_us < r->sampled_us || now_us - r->sampled_us > SENSOR_STALE_US) r->valid = false;
    }
}
