#pragma once
#include "sensor_driver.h"
typedef void (*sensor_snapshot_cb)(const sensor_snapshot_t *snapshot);
bool sensor_manager_start(i2c_master_bus_handle_t bus, const sensor_driver_t *const *drivers,
                          size_t count, sensor_snapshot_cb on_snapshot);
void sensor_manager_snapshot(sensor_snapshot_t *out);
