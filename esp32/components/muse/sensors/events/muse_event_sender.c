/* Transport reference: linux/AGENTS.md and LinkSession.send_chat. */
#include "muse_event_sender.h"
#include "../sensor_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "muse_link.h"
static const char *TAG = "sensors.events";
static SemaphoreHandle_t s_lock;
static bool s_busy, s_overflow;
static uint32_t s_generation;
static int64_t s_stream, s_started;
static int s_status;
static char s_response[512];
static size_t s_response_len;
static sensor_event_send_cb s_done;
static void *s_ctx;
static const char *s_label;

bool muse_event_sender_init(void)
{
    if (s_lock) return true;
    s_lock = xSemaphoreCreateMutex(); return s_lock != NULL;
}

bool muse_event_sender_ready(void)
{
    if (!s_lock || !muse_link_req_ready() || !muse_link_node_id()[0]) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY); bool ready = !s_busy; xSemaphoreGive(s_lock);
    return ready;
}

/* Atomically claim completion, then run rule callbacks without the transport lock. */
static bool take_completion(uint32_t generation, sensor_event_send_cb *cb, void **ctx)
{
    if (!s_busy || s_generation != generation) return false;
    *cb = s_done; *ctx = s_ctx;
    s_busy = false; s_stream = 0; ++s_generation;
    return true;
}

static void finish(uint32_t generation, bool acknowledged)
{
    sensor_event_send_cb cb = NULL; void *ctx = NULL;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool claimed = take_completion(generation, &cb, &ctx);
    xSemaphoreGive(s_lock);
    if (claimed) cb(ctx, acknowledged);
}

static void response(void *ctx, int status, const uint8_t *data, size_t len, bool end)
{
    uint32_t generation = (uint32_t)(uintptr_t)ctx;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_busy || generation != s_generation) { xSemaphoreGive(s_lock); return; }
    if (status) s_status = status;
    if (len) {
        size_t room = sizeof(s_response) - 1 - s_response_len;
        if (len > room) { len = room; s_overflow = true; }
        memcpy(s_response + s_response_len, data, len);
        s_response_len += len; s_response[s_response_len] = 0;
    }
    bool complete = end || status < 0, ok = false;
    sensor_event_send_cb done = NULL; void *owner = NULL;
    if (complete) {
        cJSON *root = cJSON_Parse(s_response);
        cJSON *result = cJSON_GetObjectItem(root, "result");
        const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(cJSON_IsObject(result) ? result : root, "message_id"));
        ok = s_status == 200 && !s_overflow && id && id[0];
        ESP_LOGI(TAG, "%s %s: HTTP %d, message_id=%s", s_label,
                 ok ? "acknowledged" : "failed", s_status, id ? id : "none");
        cJSON_Delete(root);
        take_completion(generation, &done, &owner);
    }
    xSemaphoreGive(s_lock);
    if (done) done(owner, ok);
}

void muse_event_sender_tick(int64_t now_us)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool timed_out = s_busy && now_us - s_started > (int64_t)SENSOR_EVENT_TIMEOUT_S * 1000000;
    uint32_t generation = s_generation; int64_t stream = s_stream;
    sensor_event_send_cb done = NULL; void *owner = NULL;
    if (timed_out) {
        ESP_LOGW(TAG, "%s timed out", s_label);
        take_completion(generation, &done, &owner);
    }
    xSemaphoreGive(s_lock);
    if (done) { done(owner, false); if (stream) muse_link_req_cancel(stream); }
}

bool muse_event_sender_send(const char *message, const char *label, sensor_event_send_cb cb, void *ctx)
{
    if (!s_lock || !cb || !message || !label || !muse_link_req_ready()) return false;
    const char *node = muse_link_node_id(); if (!node[0]) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_busy) { xSemaphoreGive(s_lock); return false; }
    s_busy = true; s_started = esp_timer_get_time();
    uint32_t generation = ++s_generation;
    if (!generation) generation = ++s_generation;
    s_done = cb; s_ctx = ctx; s_label = label;
    s_status = 0; s_response_len = 0; s_response[0] = 0; s_overflow = false;
    xSemaphoreGive(s_lock);
    char request_id[64];
    snprintf(request_id, sizeof(request_id), "k10-sensor-%08lx-%lu", (unsigned long)esp_random(), (unsigned long)generation);
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "message", message);
    cJSON_AddStringToObject(body, "output_modality", "text");
    cJSON_AddStringToObject(body, "device_id", node);
    char *json = cJSON_PrintUnformatted(body); cJSON_Delete(body);
    const char *headers[] = {"Content-Type", "application/json", "Accept", "application/json",
                            "x-app-id", "musegadget", "x-request-id", request_id, NULL};
    int64_t stream = json ? muse_link_req_open("POST", "/chat/stream", headers, false, response,
                                              (void *)(uintptr_t)generation) : 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_busy && generation == s_generation) s_stream = stream;
    xSemaphoreGive(s_lock);
    bool sent = stream && muse_link_req_send(stream, json, strlen(json), true, 0);
    free(json);
    if (!sent) {
        finish(generation, false);
        if (stream) muse_link_req_cancel(stream);
        ESP_LOGW(TAG, "%s could not be queued", label);
    } else ESP_LOGI(TAG, "%s sent", label);
    return true;
}
