/* Protocols: DFRobot/framework-arduinounihiker, unihiker_k10.cpp/h
 * and libraries/DFRobot_AHT20/DFRobot_AHT20.cpp. SDA47/SCL48 is supplied by the board. */
#include "../sensor_driver.h"
#include "sensor_i2c.h"
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static i2c_master_dev_handle_t s_acc;
static bool init(i2c_master_bus_handle_t bus)
{
    s_acc = sensor_i2c_add(bus, 0x19, "SC7A20H");
    /* Vendor CTRL4=0x88: BDU, high resolution, +/-2g. CTRL1=0x27. */
    return s_acc && sensor_i2c_write(s_acc, 0x23, 0x88) == ESP_OK && sensor_i2c_write(s_acc, 0x20, 0x27) == ESP_OK;
}
static int signed12(uint8_t lo, uint8_t hi)
{ int value = ((hi << 8) | lo) >> 4; return value & 0x800 ? value - 4096 : value; }

static bool read_values(sensor_value_t *values)
{
    uint8_t data[6];
    if (!s_acc || sensor_i2c_read(s_acc, 0xa8, data, 6) != ESP_OK) return false;
    values[0].axis[0] = signed12(data[0], data[1]); values[0].axis[1] = signed12(data[2], data[3]);
    values[0].axis[2] = signed12(data[4], data[5]); return true;
}

static const sensor_channel_t CHANNELS[] = {
    {.key="acceleration", .label="Accel raw12", .unit="raw_12bit", .display_unit="", .kind=SENSOR_VECTOR3,
     .note="Signed raw 12-bit X/Y/Z values, not g and not a gyroscope."},
};
const sensor_driver_t SENSOR_SC7A20H = {"SC7A20H", CHANNELS, 1, SENSOR_ACCEL_PERIOD_MS, init, read_values};
