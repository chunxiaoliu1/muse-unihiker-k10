#pragma once
#include "../sensor_types.h"
#ifdef __cplusplus
extern "C" {
#endif
bool sensor_events_init(void);
void sensor_events_tick(const sensor_snapshot_t *snapshot);
bool k10_humidity_alert_test(void); /* Clearly labeled simulation; does not override readings. */
#ifdef __cplusplus
}
#endif
