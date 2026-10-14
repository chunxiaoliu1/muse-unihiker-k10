/* UNIHIKER K10 GC2145 capture backend.
 * Wiring: DFRobot/framework-arduinounihiker, tools/sdk/esp32s3/include/modules/
 * camera/who_camera.h. SCCB shares I2C0; the board owns XL9535 P01 reset.
 */
#include "gc2145.h"
#include "../k10_camera_config.h"
#include <stdlib.h>
#include <string.h>
#include "camera.h"
#include "driver/i2c_master.h"
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "img_converters.h"

static const char *TAG = "k10.camera";
static k10_camera_reset_fn s_reset;

typedef struct {
    uint8_t *data;
    size_t len;
    bool overflow;
} jpeg_output_t;

/* esp32-camera's encoder ignores short callback writes, so track overflow
 * separately and discard the entire encoded image if it exceeds the limit. */
static size_t jpeg_write(void *arg, size_t index, const void *data, size_t len)
{
    jpeg_output_t *out = arg;
    if (out->overflow || index > K10_CAMERA_JPEG_MAX ||
        len > K10_CAMERA_JPEG_MAX - index) {
        out->overflow = true;
        return 0;
    }
    if (len) memcpy(out->data + index, data, len);
    out->len = index + len;
    return len;
}

static esp_err_t capture(camera_frame_t *out)
{
    if (!s_reset) return ESP_ERR_INVALID_STATE;
    esp_err_t err = s_reset(false);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(K10_CAMERA_RESET_HOLD_MS));
    err = s_reset(true);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(K10_CAMERA_RESET_RELEASE_MS));

    const camera_config_t config = {
        .pin_pwdn = -1, .pin_reset = -1, .pin_xclk = K10_CAMERA_PIN_XCLK,
        .pin_sccb_sda = -1, .pin_sccb_scl = -1, .sccb_i2c_port = I2C_NUM_0,
        .pin_d0 = K10_CAMERA_PIN_D0, .pin_d1 = K10_CAMERA_PIN_D1, .pin_d2 = K10_CAMERA_PIN_D2, .pin_d3 = K10_CAMERA_PIN_D3,
        .pin_d4 = K10_CAMERA_PIN_D4, .pin_d5 = K10_CAMERA_PIN_D5, .pin_d6 = K10_CAMERA_PIN_D6, .pin_d7 = K10_CAMERA_PIN_D7,
        .pin_vsync = K10_CAMERA_PIN_VSYNC, .pin_href = K10_CAMERA_PIN_HREF, .pin_pclk = K10_CAMERA_PIN_PCLK,
        .xclk_freq_hz = K10_CAMERA_XCLK_HZ,
        .ledc_timer = LEDC_TIMER_1, .ledc_channel = LEDC_CHANNEL_1,
        .pixel_format = PIXFORMAT_RGB565, .frame_size = K10_CAMERA_FRAME_SIZE,
        .fb_count = 1, .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
    };
    bool started = false;
    camera_fb_t *fb = NULL;
    jpeg_output_t jpeg = {0};
    ESP_LOGI(TAG, "start capture: internal free=%u largest=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    err = esp_camera_init(&config);
    if (err != ESP_OK) goto done; /* Driver cleans up its failed init. */
    started = true;
#if INCLUDE_xTaskGetHandle
    /* The upstream driver doesn't check its task-creation result. Detect a
     * failed task allocation instead of waiting on a queue with no consumer. */
    if (!xTaskGetHandle("cam_task")) {
        ESP_LOGW(TAG, "camera task unavailable: internal largest=%u",
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        err = ESP_ERR_NO_MEM;
        goto done;
    }
#endif
    sensor_t *sensor = esp_camera_sensor_get();
    if (!sensor || sensor->id.PID != GC2145_PID) {
        err = ESP_ERR_NOT_SUPPORTED;
        goto done;
    }
    /* Let auto-exposure settle; all buffers belong to this request, so no
     * cached image from a previous conversation can be returned. */
    for (int i = 0; i <= K10_CAMERA_WARMUP_FRAMES; ++i) {
        fb = esp_camera_fb_get();
        if (!fb) { err = ESP_ERR_TIMEOUT; goto done; }
        if (i < K10_CAMERA_WARMUP_FRAMES) {
            esp_camera_fb_return(fb);
            fb = NULL;
            vTaskDelay(pdMS_TO_TICKS(K10_CAMERA_WARMUP_DELAY_MS));
        }
    }
    if (fb->format != PIXFORMAT_RGB565 || fb->width != K10_CAMERA_WIDTH ||
        fb->height != K10_CAMERA_HEIGHT || fb->len != fb->width * fb->height * 2) {
        err = ESP_ERR_INVALID_SIZE;
        goto done;
    }
    jpeg.data = heap_caps_malloc(K10_CAMERA_JPEG_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!jpeg.data) { err = ESP_ERR_NO_MEM; goto done; }
    bool encoded = frame2jpg_cb(fb, K10_CAMERA_JPEG_QUALITY, jpeg_write, &jpeg);
    if (jpeg.overflow) {
        jpeg.len = 0;
        jpeg.overflow = false;
        encoded = frame2jpg_cb(fb, K10_CAMERA_JPEG_FALLBACK_QUALITY, jpeg_write, &jpeg);
    }
    if (!encoded || jpeg.overflow || jpeg.len < 4 || jpeg.data[0] != 0xff ||
        jpeg.data[1] != 0xd8 || jpeg.data[jpeg.len - 2] != 0xff ||
        jpeg.data[jpeg.len - 1] != 0xd9) {
        err = jpeg.overflow ? ESP_ERR_INVALID_SIZE : ESP_FAIL;
        goto done;
    }
    out->jpeg = jpeg.data;
    out->len = jpeg.len;
    out->width = fb->width;
    out->height = fb->height;
    jpeg.data = NULL;
    err = ESP_OK;
done:
    free(jpeg.data);
    if (fb) esp_camera_fb_return(fb);
    if (started) {
        esp_err_t stop = esp_camera_deinit();
        if (stop != ESP_OK) ESP_LOGW(TAG, "camera stop: %s", esp_err_to_name(stop));
    }
    /* Stop XCLK/free DMA and RGB buffers before returning the JPEG. Keep the
     * sensor in reset between requests; the shared I2C bus remains installed. */
    esp_err_t reset = s_reset(false);
    if (reset != ESP_OK) ESP_LOGW(TAG, "camera reset: %s", esp_err_to_name(reset));
    return err;
}

static void release(camera_frame_t *frame) { free((void *)frame->jpeg); }

void k10_gc2145_register(k10_camera_reset_fn reset)
{
    static const camera_driver_t driver = {
        .name = "GC2145 (UNIHIKER K10)",
        .max_width = K10_CAMERA_WIDTH, .max_height = K10_CAMERA_HEIGHT,
        .capture = capture, .release = release,
    };
    s_reset = reset;
    camera_register(&driver);
}

