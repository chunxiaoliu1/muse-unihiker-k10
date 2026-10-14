#pragma once
#include "driver/i2c_master.h"
i2c_master_dev_handle_t sensor_i2c_add(i2c_master_bus_handle_t bus, int address, const char *name);
esp_err_t sensor_i2c_read(i2c_master_dev_handle_t dev, uint8_t reg, void *out, size_t size);
esp_err_t sensor_i2c_write(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value);
