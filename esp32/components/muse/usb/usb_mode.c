#include "usb_mode.h"

#include <stdatomic.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/idf_additions.h"
#include "muse_console.h"
#include "muse_link.h"

enum { USB_OFF, USB_STARTING, USB_ON, USB_FAILED };
static atomic_int s_mode;
static TaskFunction_t s_serial_entry;
static const char *TAG = "muse.usb";

void muse_usb_mode_init(TaskFunction_t serial_entry, bool sdk_ready)
{
    s_serial_entry = serial_entry;
    // No driver, request queue, task stack or persistent flag here.
    ESP_LOGI(TAG, "boot: USB resources not allocated; token=%s driver=%d",
             sdk_ready ? "configured" : "missing", usb_serial_jtag_is_driver_installed());
    if (!sdk_ready) muse_usb_mode_request();
}

bool muse_usb_mode_request(void)
{
    int previous = atomic_load(&s_mode);
    for (;;) {
        if (previous == USB_ON || previous == USB_STARTING) return true;
        if (atomic_compare_exchange_weak(&s_mode, &previous, USB_STARTING)) break;
    }
    if (!s_serial_entry || !muse_link_usb_mode_request()) {
        atomic_store(&s_mode, USB_FAILED);
        return false;
    }
    return true;
}

bool muse_usb_mode_enabled(void)
{
    int state = atomic_load(&s_mode);
    return state == USB_ON || state == USB_STARTING;
}

const char *muse_usb_mode_name(void)
{
    static const char *const names[] = { "Off", "Starting", "On", "Failed" };
    return names[atomic_load(&s_mode)];
}

esp_err_t muse_usb_mode_start(bool credentials_ready)
{
    if (atomic_load(&s_mode) == USB_ON) return ESP_OK;
    esp_err_t error = credentials_ready ? ESP_OK : ESP_ERR_NO_MEM;
    if (!s_serial_entry) error = ESP_ERR_INVALID_STATE;
    if (error == ESP_OK) error = muse_console_install(1024);
    if (error == ESP_OK) {
        if (xTaskCreateWithCaps(s_serial_entry, "muse_serial", 3584, NULL, 5, NULL,
                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
            // Nothing has consumed the transport yet. Roll back the VFS fd.
            muse_console_close();
            error = ESP_ERR_NO_MEM;
        }
    }
    atomic_store(&s_mode, error == ESP_OK ? USB_ON : USB_FAILED);
    ESP_LOGI(TAG, "configuration %s: transport=fifo/vfs driver=%d task=%d queue=%d internal=%u psram=%u",
             muse_usb_mode_name(), usb_serial_jtag_is_driver_installed(),
             error == ESP_OK, error == ESP_OK,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return error;
}
