#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include "../sensor_config.h"

#define K10_HUMIDITY_REPEAT_US ((int64_t)SENSOR_HUMIDITY_REPEAT_S * 1000000)
#define K10_HUMIDITY_RETRY_US ((int64_t)SENSOR_EVENT_RETRY_S * 1000000)
typedef struct { bool active; int64_t next_us; } k10_humidity_policy_t;

static inline bool k10_humidity_due(k10_humidity_policy_t *p, bool valid, double rh, int64_t now)
{
    if (!valid || !isfinite(rh) || rh < 0 || rh > 100) return false;
    if (rh <= SENSOR_HUMIDITY_REARM_RH) { p->active = false; p->next_us = 0; return false; }
    if (rh <= SENSOR_HUMIDITY_TRIGGER_RH) return false;
    if (!p->active) { p->active = true; p->next_us = 0; }
    return now >= p->next_us;
}

/* Runs on either a host harness or K10: boundaries, repeat, recovery, retry,
 * invalid readings and the hysteresis band must obey the user-facing policy. */
static inline bool k10_humidity_policy_test(void)
{
    k10_humidity_policy_t p = {0};
    double high = SENSOR_HUMIDITY_TRIGGER_RH;
    double reset = SENSOR_HUMIDITY_REARM_RH;
    double mid = (high + reset) / 2;
    double above = (high + 100) / 2;
    if (high < 0 || high >= 100 || reset < 0 || reset > high ||
        SENSOR_HUMIDITY_REPEAT_S <= 0 || SENSOR_EVENT_RETRY_S <= 0) return false;
    if (k10_humidity_due(&p, true, high, 1)) return false;
    if (!k10_humidity_due(&p, true, above, 2)) return false;
    p.next_us = 2 + K10_HUMIDITY_REPEAT_US;
    if (k10_humidity_due(&p, true, above, p.next_us - 1)) return false;
    if (!k10_humidity_due(&p, true, above, p.next_us)) return false;
    if (k10_humidity_due(&p, true, mid, p.next_us)) return false;
    if (k10_humidity_due(&p, false, 90, p.next_us)) return false;
    if (k10_humidity_due(&p, true, NAN, p.next_us)) return false;
    if (k10_humidity_due(&p, true, 101, p.next_us)) return false;
    if (k10_humidity_due(&p, true, reset, 5) || p.active) return false;
    if (!k10_humidity_due(&p, true, above, 6)) return false;
    p.next_us = 6 + K10_HUMIDITY_RETRY_US;
    return !k10_humidity_due(&p, true, above, p.next_us - 1)
        && k10_humidity_due(&p, true, above, p.next_us);
}
