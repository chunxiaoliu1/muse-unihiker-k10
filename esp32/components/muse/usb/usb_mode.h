#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* K10 configuration transport. The flag is RAM-only and never saved to NVS.
 * Only the internal-stack keeper starts resources; UI calls request(). */
void muse_usb_mode_init(TaskFunction_t serial_entry, bool sdk_ready);
bool muse_usb_mode_request(void);
bool muse_usb_mode_enabled(void);
const char *muse_usb_mode_name(void);
esp_err_t muse_usb_mode_start(bool credentials_ready);
