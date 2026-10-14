#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Context and label must remain valid until the completion callback. */
typedef void (*sensor_event_send_cb)(void *ctx, bool acknowledged);
bool muse_event_sender_init(void);
bool muse_event_sender_ready(void);
void muse_event_sender_tick(int64_t now_us);
/* False: busy/offline, no callback. True: accepted attempt, callback finishes it. */
bool muse_event_sender_send(const char *message, const char *label, sensor_event_send_cb cb, void *ctx);
