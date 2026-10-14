#include "sensor_manager.h"
#include <math.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "muse_mem.h"

static const char *TAG = "sensors.manager";
static SemaphoreHandle_t s_lock;
static sensor_snapshot_cb s_on_snapshot;
EXT_RAM_BSS_ATTR static sensor_snapshot_t s_snapshot;
static struct {
    const sensor_driver_t *driver;
    size_t first;
    bool enabled;
    int64_t next_us;
} s_drivers[SENSOR_MAX_DRIVERS];
static size_t s_driver_count;

void sensor_manager_snapshot(sensor_snapshot_t *out)
{
    if (!s_lock) { *out = (sensor_snapshot_t){0}; return; }
    xSemaphoreTake(s_lock, portMAX_DELAY); *out = s_snapshot; xSemaphoreGive(s_lock);
    sensor_snapshot_expire(out, esp_timer_get_time());
}

static void task(void *arg)
{
    (void)arg;
    for (;;) {
        for (size_t i = 0; i < s_driver_count; ++i) {
            const sensor_driver_t *driver = s_drivers[i].driver;
            int64_t now = esp_timer_get_time();
            if (!s_drivers[i].enabled || now < s_drivers[i].next_us) continue;
            s_drivers[i].next_us = now + (int64_t)driver->period_ms * 1000;
            sensor_value_t values[SENSOR_MAX_DRIVER_CHANNELS] = {0};
            bool valid = driver->read(values);
            int64_t sampled = esp_timer_get_time();
            xSemaphoreTake(s_lock, portMAX_DELAY);
            for (size_t j = 0; j < driver->channel_count; ++j) {
                sensor_reading_t *reading = &s_snapshot.readings[s_drivers[i].first + j];
                bool finite = isfinite(values[j].axis[0]);
                if (reading->channel->kind == SENSOR_VECTOR3)
                    finite &= isfinite(values[j].axis[1]) && isfinite(values[j].axis[2]);
                reading->valid = valid && finite;
                if (reading->valid) { reading->value = values[j]; reading->sampled_us = sampled; }
            }
            xSemaphoreGive(s_lock);
        }
        if (s_on_snapshot) {
            sensor_snapshot_t snapshot;
            sensor_manager_snapshot(&snapshot);
            s_on_snapshot(&snapshot); /* No cache lock held during event/network processing. */
        }
        vTaskDelay(pdMS_TO_TICKS(SENSOR_TASK_PERIOD_MS));
    }
}

bool sensor_manager_start(i2c_master_bus_handle_t bus, const sensor_driver_t *const *drivers,
                          size_t count, sensor_snapshot_cb on_snapshot)
{
    if (s_lock || !drivers || !count || count > SENSOR_MAX_DRIVERS ||
        SENSOR_TASK_PERIOD_MS <= 0 || SENSOR_STALE_US <= 0) return false;
    size_t channels = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!drivers[i] || !drivers[i]->channels || !drivers[i]->read || !drivers[i]->init || !drivers[i]->period_ms ||
            !drivers[i]->channel_count || drivers[i]->channel_count > SENSOR_MAX_DRIVER_CHANNELS) return false;
        channels += drivers[i]->channel_count;
    }
    if (channels > SENSOR_MAX_READINGS) return false;
    /* Duplicate keys would silently overwrite Muse fields: reject before touching hardware. */
    for (size_t i = 0; i < count; ++i) for (size_t j = 0; j < drivers[i]->channel_count; ++j) {
        const sensor_channel_t *channel = &drivers[i]->channels[j];
        const char *key = channel->key;
        if (!key || !key[0] || !channel->label || !channel->unit || !channel->display_unit ||
            (channel->kind != SENSOR_NUMBER && channel->kind != SENSOR_VECTOR3)) return false;
        for (size_t k = 0; k <= i; ++k) for (size_t l = 0; l < drivers[k]->channel_count; ++l) {
            if (k == i && l >= j) break;
            if (!strcmp(key, drivers[k]->channels[l].key)) return false;
        }
    }
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return false;
    sensor_snapshot_t initial = {0};
    for (size_t i = 0; i < count; ++i) {
        s_drivers[i].driver = drivers[i]; s_drivers[i].first = initial.count;
        s_drivers[i].enabled = drivers[i]->init(bus);
        for (size_t j = 0; j < drivers[i]->channel_count; ++j)
            initial.readings[initial.count++].channel = &drivers[i]->channels[j];
    }
    xSemaphoreTake(s_lock, portMAX_DELAY); s_snapshot = initial; xSemaphoreGive(s_lock);
    s_driver_count = count; s_on_snapshot = on_snapshot;
    if (xTaskCreateWithCaps(task, "k10_sensors", SENSOR_TASK_STACK_BYTES, NULL, 2, NULL, MUSE_BIG_CAPS) != pdPASS) {
        ESP_LOGE(TAG, "Sensor task allocation failed"); return false;
    }
    return true;
}
