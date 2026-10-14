/* Protocols: DFRobot/framework-arduinounihiker, unihiker_k10.cpp/h
 * and libraries/DFRobot_AHT20/DFRobot_AHT20.cpp. SDA47/SCL48 is supplied by the board. */
#include "../sensor_driver.h"
#include "sensor_i2c.h"
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static i2c_master_dev_handle_t s_aht;
static bool init(i2c_master_bus_handle_t bus)
{
    s_aht = sensor_i2c_add(bus, 0x38, "AHT20");
    if (!s_aht) return false;
    uint8_t status;
    if (sensor_i2c_read(s_aht, 0x71, &status, 1) != ESP_OK) return false;
    if (!(status & 8)) {
        uint8_t command[] = {0xbe, 8, 0};
        if (i2c_master_transmit(s_aht, command, 3, 100) != ESP_OK) return false;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return true;
}
static uint8_t crc8(const uint8_t *data, size_t n)
{
    uint8_t crc = 0xff;
    for (size_t i = 0; i < n; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : crc << 1;
    }
    return crc;
}

static bool read_values(sensor_value_t *values)
{
    uint8_t command[] = {0xac, 0x33, 0}, data[7];
    if (!s_aht || i2c_master_transmit(s_aht, command, 3, 100) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(90)); /* Wait outside the bus transaction. */
    if (i2c_master_receive(s_aht, data, 7, 100) != ESP_OK ||
        (data[0] & 0x80) || !(data[0] & 8) || crc8(data, 6) != data[6]) return false;
    uint32_t rh = ((uint32_t)data[1] << 12) | ((uint32_t)data[2] << 4) | (data[3] >> 4);
    uint32_t temp = ((uint32_t)(data[3] & 15) << 16) | ((uint32_t)data[4] << 8) | data[5];
    values[1].axis[0] = rh * 100.0f / 1048576.0f;
    values[0].axis[0] = temp * 200.0f / 1048576.0f - 50.0f;
    return true;
}

static const sensor_channel_t CHANNELS[] = {
    {.key="temperature", .label="Temperature", .unit="celsius", .display_unit="C", .kind=SENSOR_NUMBER,
     .note="Onboard temperature may be warmed by the board."},
    {.key="humidity", .label="Humidity", .unit="percent_rh", .display_unit="%RH", .kind=SENSOR_NUMBER},
};
const sensor_driver_t SENSOR_AHT20 = {"AHT20", CHANNELS, 2, SENSOR_CLIMATE_PERIOD_MS, init, read_values};
