/* Protocols: DFRobot/framework-arduinounihiker, unihiker_k10.cpp/h
 * and libraries/DFRobot_AHT20/DFRobot_AHT20.cpp. SDA47/SCL48 is supplied by the board. */
#include "../sensor_driver.h"
#include "sensor_i2c.h"
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static i2c_master_dev_handle_t s_ltr;
static bool init(i2c_master_bus_handle_t bus)
{
    s_ltr = sensor_i2c_add(bus, 0x29, "LTR303");
    return s_ltr && sensor_i2c_write(s_ltr, 0x80, 1) == ESP_OK && sensor_i2c_write(s_ltr, 0x85, 3) == ESP_OK;
}
/* Vendor coefficients: floating-point channel ratio, gain 1x, integration 100ms. */
static bool read_values(sensor_value_t *values)
{
    uint8_t data[4];
    if (!s_ltr || sensor_i2c_read(s_ltr, 0x88, data, 4) != ESP_OK) return false;
    values[0].raw[1] = data[0] | (data[1] << 8); values[0].raw[0] = data[2] | (data[3] << 8);
    float a = values[0].raw[0], b = values[0].raw[1], ratio = a + b > 0 ? b / (a + b) : 1.0f;
    values[0].axis[0] = ratio < .45f ? 1.7743f*a + 1.1059f*b :
             ratio < .64f ? 4.2785f*a - 1.9548f*b :
             ratio < .85f ? .5926f*a + .1185f*b : 0;
    values[0].axis[0] = fmaxf(values[0].axis[0], 0);
    return true;
}

static const sensor_channel_t CHANNELS[] = {
    {.key="light", .label="Light est.", .unit="lux", .display_unit="lx", .kind=SENSOR_NUMBER, .estimated=true},
};
const sensor_driver_t SENSOR_LTR303 = {"LTR303", CHANNELS, 1, SENSOR_LIGHT_PERIOD_MS, init, read_values};
