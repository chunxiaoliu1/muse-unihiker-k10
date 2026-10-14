#include "k10_catalog.h"
extern const sensor_driver_t SENSOR_AHT20;
extern const sensor_driver_t SENSOR_LTR303;
extern const sensor_driver_t SENSOR_SC7A20H;

/* Add new physical sensors here. Each driver supplies its channels and period;
 * cache, JSON output and command description enumerate this same catalog. */
const sensor_driver_t *const K10_SENSOR_DRIVERS[] = {
    &SENSOR_AHT20,
    &SENSOR_LTR303,
    &SENSOR_SC7A20H,
};
const size_t K10_SENSOR_DRIVER_COUNT = sizeof(K10_SENSOR_DRIVERS) / sizeof(K10_SENSOR_DRIVERS[0]);
