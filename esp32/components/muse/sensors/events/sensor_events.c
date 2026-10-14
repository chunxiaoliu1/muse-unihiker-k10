#include "sensor_events.h"
#include "k10_humidity_policy.h"
#include "muse_event_sender.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* Add new event rules here, or call another rule module from this dispatcher.
 * Rules consume cached snapshots; drivers and message delivery are separate. */
static SemaphoreHandle_t s_lock;
static k10_humidity_policy_t s_humidity;
static bool s_busy, s_test_pending;

bool sensor_events_init(void)
{
    if (s_lock) return true;
    if (SENSOR_HUMIDITY_TRIGGER_RH < 0 || SENSOR_HUMIDITY_TRIGGER_RH >= 100 ||
        SENSOR_HUMIDITY_REARM_RH < 0 || SENSOR_HUMIDITY_REARM_RH > SENSOR_HUMIDITY_TRIGGER_RH ||
        SENSOR_HUMIDITY_REPEAT_S <= 0 || SENSOR_EVENT_RETRY_S <= 0 || SENSOR_EVENT_TIMEOUT_S <= 0) return false;
    if (!muse_event_sender_init()) return false;
    s_lock = xSemaphoreCreateMutex(); return s_lock != NULL;
}

bool k10_humidity_alert_test(void)
{
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool available = !s_busy && !s_test_pending;
    if (available) s_test_pending = true;
    xSemaphoreGive(s_lock); return available;
}

static void humidity_completed(void *ctx, bool acknowledged)
{
    bool test = (uintptr_t)ctx != 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!test) s_humidity.next_us = esp_timer_get_time() +
        (acknowledged ? K10_HUMIDITY_REPEAT_US : K10_HUMIDITY_RETRY_US);
    s_busy = false;
    xSemaphoreGive(s_lock);
}

void sensor_events_tick(const sensor_snapshot_t *snapshot)
{
    if (!s_lock) return;
    int64_t now = esp_timer_get_time();
    muse_event_sender_tick(now);
    const sensor_reading_t *humidity = sensor_snapshot_find(snapshot, "humidity");
    bool fresh = humidity && humidity->valid && humidity->channel->kind == SENSOR_NUMBER
        && !strcmp(humidity->channel->unit, "percent_rh");
    double rh = fresh ? humidity->value.axis[0] : 0;
    bool ready = muse_event_sender_ready();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool due = k10_humidity_due(&s_humidity, fresh, rh, now);
    if (s_busy || !ready || (!due && !s_test_pending)) { xSemaphoreGive(s_lock); return; }
    bool test = s_test_pending; s_test_pending = false; s_busy = true;
    xSemaphoreGive(s_lock);
    char message[256], cadence[32];
    if (SENSOR_HUMIDITY_REPEAT_S % 60 == 0)
        snprintf(cadence, sizeof(cadence), "%d minutes", SENSOR_HUMIDITY_REPEAT_S / 60);
    else snprintf(cadence, sizeof(cadence), "%d seconds", SENSOR_HUMIDITY_REPEAT_S);
    double simulated_rh = fmin(SENSOR_HUMIDITY_TRIGGER_RH + 5, 100);
    if (test) snprintf(message, sizeof(message),
        "[K10 TEST alert - simulated reading] Humidity is too high: %.1f%% RH (threshold %.1f%% RH). This is not a real measurement.",
        simulated_rh, SENSOR_HUMIDITY_TRIGGER_RH);
    else snprintf(message, sizeof(message),
        "[UNIHIKER K10 humidity alert] Ambient humidity is too high: %.2f%% RH (threshold %.1f%% RH). Repeats every %s while high.",
        rh, SENSOR_HUMIDITY_TRIGGER_RH, cadence);
    if (!muse_event_sender_send(message, test ? "humidity alert TEST" : "humidity alert",
                                 humidity_completed, (void *)(uintptr_t)test))
        humidity_completed((void *)(uintptr_t)test, false);
}
