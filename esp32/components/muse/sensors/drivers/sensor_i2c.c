#include "sensor_i2c.h"
#include "esp_log.h"
static const char *TAG = "k10.sensors";
esp_err_t sensor_i2c_read(i2c_master_dev_handle_t dev, uint8_t reg, void *out, size_t size)
{ return i2c_master_transmit_receive(dev, &reg, 1, out, size, 100); }
esp_err_t sensor_i2c_write(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value)
{ uint8_t data[] = {reg, value}; return i2c_master_transmit(dev, data, 2, 100); }
i2c_master_dev_handle_t sensor_i2c_add(i2c_master_bus_handle_t bus, int address, const char *name)
{
    if (i2c_master_probe(bus, address, 100) != ESP_OK) {
        ESP_LOGW(TAG, "%s unavailable at 0x%02x", name, address); return NULL;
    }
    i2c_master_dev_handle_t dev = NULL;
    i2c_device_config_t cfg = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address, .scl_speed_hz = 100000};
    if (i2c_master_bus_add_device(bus, &cfg, &dev) != ESP_OK) return NULL;
    ESP_LOGI(TAG, "%s detected at 0x%02x", name, address);
    return dev;
}
