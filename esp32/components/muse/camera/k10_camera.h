#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The board owns XL9535 and its output latch; never write it independently. */
typedef esp_err_t (*k10_camera_reset_fn)(bool released);
void k10_camera_register(k10_camera_reset_fn reset);
bool k10_camera_active(void);
/* Slow: call from a worker, never the Link or button-poll task. */
cJSON *k10_camera_command(void);
bool k10_camera_serial_capture(void);
const char *k10_camera_description(void);

#ifdef __cplusplus
}
#endif
