#pragma once
#include <stddef.h>
#include "cJSON.h"
#include "driver/i2c_master.h"
#include "events/sensor_events.h"

/* Board wiring and presentation entry points; drivers/rules live below this directory. */
#ifdef __cplusplus
extern "C" {
#endif
void k10_sensors_start(i2c_master_bus_handle_t bus);
void k10_sensors_text(char *out, size_t size);
void k10_sensors_print(void);
cJSON *k10_sensors_command(void);
const char *k10_sensors_description(void);
#ifdef __cplusplus
}
#endif
