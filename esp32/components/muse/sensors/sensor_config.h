#pragma once

/* Sampling and capacity. Periods are milliseconds; validity is microseconds. */
#define SENSOR_MAX_DRIVERS 8
#define SENSOR_MAX_READINGS 16
#define SENSOR_MAX_DRIVER_CHANNELS 4
#define SENSOR_TASK_PERIOD_MS 200
#define SENSOR_TASK_STACK_BYTES 6144
#define SENSOR_LOG_PERIOD_MS 2000
#define SENSOR_CLIMATE_PERIOD_MS 2000
#define SENSOR_LIGHT_PERIOD_MS 200
#define SENSOR_ACCEL_PERIOD_MS 200
#define SENSOR_STALE_US (10LL * 1000000)
#define SENSOR_DESCRIPTION_BYTES 2048

/* Humidity event rule. Changes take effect after rebuilding/flashing. */
#define SENSOR_HUMIDITY_TRIGGER_RH 70.0
#define SENSOR_HUMIDITY_REARM_RH 65.0
#define SENSOR_HUMIDITY_REPEAT_S 900
#define SENSOR_EVENT_RETRY_S 60
#define SENSOR_EVENT_TIMEOUT_S 45
