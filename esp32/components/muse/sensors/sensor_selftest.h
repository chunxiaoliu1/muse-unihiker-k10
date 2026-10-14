#pragma once
#include "sensor_types.h"

/* Fixtures are local to this test: they never enter the live cache or Muse. */
static inline bool sensor_snapshot_selftest(void)
{
    static const sensor_channel_t channels[] = {
        {.key="humidity", .label="Humidity", .unit="percent_rh", .display_unit="%RH", .kind=SENSOR_NUMBER},
        {.key="extra_sensor", .label="Extra", .unit="test_unit", .display_unit="u", .kind=SENSOR_NUMBER},
    };
    sensor_snapshot_t snapshot = {.count = 2};
    snapshot.readings[0] = (sensor_reading_t){.channel=&channels[0], .valid=true, .sampled_us=1};
    snapshot.readings[1] = (sensor_reading_t){.channel=&channels[1], .valid=true, .sampled_us=2};
    if (sensor_snapshot_find(&snapshot, "extra_sensor") != &snapshot.readings[1] ||
        sensor_snapshot_find(&snapshot, "missing")) return false;
    sensor_snapshot_expire(&snapshot, SENSOR_STALE_US + 2);
    if (snapshot.readings[0].valid || !snapshot.readings[1].valid) return false;
    snapshot.readings[1].valid = false;
    sensor_snapshot_expire(&snapshot, 2);
    if (snapshot.readings[1].valid) return false;
    snapshot.readings[1].valid = true;
    sensor_snapshot_expire(&snapshot, 1); /* A future timestamp is not a fresh sample. */
    return !snapshot.readings[1].valid;
}
