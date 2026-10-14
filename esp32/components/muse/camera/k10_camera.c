/* K10 camera feature: Muse result, captions, busy state and USB diagnostics. */
#include "k10_camera.h"
#include "k10_camera_config.h"
#include "drivers/gc2145.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "camera.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "muse_state.h"

static const char *TAG = "k10.camera";
static atomic_bool s_active;

void k10_camera_register(k10_camera_reset_fn reset)
{
    k10_gc2145_register(reset);
}

bool k10_camera_active(void) { return atomic_load(&s_active); }

static cJSON *failure(const char *code, const char *message)
{
    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", false);
    cJSON *error = cJSON_AddObjectToObject(result, "error");
    cJSON_AddStringToObject(error, "code", code);
    cJSON_AddStringToObject(error, "message", message);
    return result;
}

cJSON *k10_camera_command(void)
{
    if (atomic_exchange(&s_active, true)) return failure("camera_busy", "camera is taking another photo");
    muse_state_set_caption("Taking a photo. Hold steady.");
    int64_t start = esp_timer_get_time();
    camera_frame_t frame;
    esp_err_t err = camera_capture(&frame);
    char *base64 = NULL;
    size_t bytes = 0, written = 0;
    int width = 0, height = 0;
    if (err == ESP_OK) {
        bytes = frame.len;
        width = frame.width; height = frame.height;
        size_t capacity = (bytes + 2) / 3 * 4 + 1;
        base64 = heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!base64) err = ESP_ERR_NO_MEM;
        else if (mbedtls_base64_encode((unsigned char *)base64, capacity, &written,
                                      frame.jpeg, frame.len) != 0) err = ESP_FAIL;
        camera_release(&frame);
    }
    cJSON *result = NULL;
    if (err == ESP_OK) {
        base64[written] = '\0';
        result = cJSON_CreateObject();
        cJSON *payload = cJSON_AddObjectToObject(result, "payload");
        if (!result || !payload || !cJSON_AddBoolToObject(result, "ok", true) ||
            !cJSON_AddStringToObject(payload, "format", "jpeg-base64") ||
            !cJSON_AddStringToObject(payload, "mime_type", "image/jpeg") ||
            !cJSON_AddStringToObject(payload, "data_base64", base64) ||
            !cJSON_AddNumberToObject(payload, "width", width) ||
            !cJSON_AddNumberToObject(payload, "height", height) ||
            !cJSON_AddNumberToObject(payload, "bytes", bytes)) {
            cJSON_Delete(result); result = NULL; err = ESP_ERR_NO_MEM;
        }
    }
    free(base64);
    if (err != ESP_OK) {
        const char *message = err == ESP_ERR_TIMEOUT ? "camera did not produce a frame"
            : err == ESP_ERR_NO_MEM ? "not enough memory to capture the photo"
            : err == ESP_ERR_NOT_SUPPORTED ? "GC2145 camera not detected"
            : err == ESP_ERR_INVALID_SIZE ? "camera frame invalid or JPEG exceeds 96 KB"
            : "camera capture failed";
        result = failure("camera_capture_failed", message);
        muse_state_set_caption("Photo failed. Please retry.");
        ESP_LOGW(TAG, "capture failed: %s", esp_err_to_name(err));
    } else {
        muse_state_set_caption("Photo sent. Waiting for Muse.");
        ESP_LOGI(TAG, "captured %dx%d JPEG, %u bytes in %lld ms", width, height,
                 (unsigned)bytes, (esp_timer_get_time() - start) / 1000);
    }
    atomic_store(&s_active, false);
    return result;
}

static void serial_capture_task(void *arg)
{
    (void)arg;
    cJSON *result = k10_camera_command();
    char *json = result ? cJSON_PrintUnformatted(result) : NULL;
    if (json) { printf("@camera.capture %s\n", json); fflush(stdout); }
    else { printf("@camera.capture {\"ok\":false,\"error\":{\"message\":\"out of memory\"}}\n"); fflush(stdout); }
    free(json); cJSON_Delete(result);
    vTaskDeleteWithCaps(NULL);
}

bool k10_camera_serial_capture(void)
{
    return xTaskCreateWithCaps(serial_capture_task, "camera_test", K10_CAMERA_WORKER_STACK,
        NULL, 4, NULL, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS;
}

#define STRINGIFY_VALUE_(value) #value
#define STRINGIFY_VALUE(value) STRINGIFY_VALUE_(value)

const char *k10_camera_description(void)
{
    return "Take a fresh photo of what the UNIHIKER K10 camera is pointing at. "
        "Use when the user asks you to look at or identify an object or read visible text. "
        "Returns one " STRINGIFY_VALUE(K10_CAMERA_WIDTH) "x" STRINGIFY_VALUE(K10_CAMERA_HEIGHT)
        " JPEG as payload.data_base64, format jpeg-base64, "
        "mime_type image/jpeg, width, height and bytes. Only captures when invoked; "
        "the user should keep the camera aimed until the photo is taken.";
}
