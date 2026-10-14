#pragma once
#include "driver/i2c_master.h"
#include "sensor_types.h"

typedef struct {
    const char *name;
    const sensor_channel_t *channels;
    size_t channel_count;
    unsigned period_ms;
    bool (*init)(i2c_master_bus_handle_t bus);
    bool (*read)(sensor_value_t *values); /* channel_count elements, supplied by manager */
} sensor_driver_t;
