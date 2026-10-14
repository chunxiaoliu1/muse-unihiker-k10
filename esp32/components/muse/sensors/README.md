# Sensors and event handling

K10 sensor logic lives here. The board supplies the shared I2C bus; the menu, serial tools and Muse consume one common data interface.

```text
sensors/
  sensor_config.h          Sampling, capacities, expiry and event parameters
  sensor_types.h           Channels, scalar/vector values, snapshots, lookup and expiry
  sensor_driver.h          Driver initialization, reads and channel descriptions
  sensor_manager.c         Periodic sampling, cache and locking; no UI or network logic
  k10_catalog.c            K10 driver registry
  k10_sensors.c            Startup wiring, Muse JSON, compatible serial output, screen text
  drivers/
    sensor_i2c.c           Shared I2C operations
    aht20.c                Temperature and humidity
    ltr303.c               Ambient light
    sc7a20h.c              Three-axis acceleration
  events/
    k10_humidity_policy.h  Pure threshold, hysteresis, repeat and retry decisions
    sensor_events.c        Scheduling, messages and success/failure timing
    muse_event_sender.c    Background delivery, ACKs, timeout, stale callback rejection
  sensor_selftest.h        Isolated test snapshots; no real cache writes or Muse messages
```

Data flows from drivers to manager snapshots, then to queries, display and event rules, and finally to the message sender. Separate locks protect the cache, rule state and sender state. I2C waits and network operations do not hold the cache lock; completion callbacks do not hold the sender lock.

## Change event rules

Edit `sensor_config.h`:

| Parameter | Current value | Meaning |
|---|---:|---|
| `SENSOR_HUMIDITY_TRIGGER_RH` | 70 | Trigger strictly above this humidity |
| `SENSOR_HUMIDITY_REARM_RH` | 65 | Rearm the first reminder at or below this value |
| `SENSOR_HUMIDITY_REPEAT_S` | 900 | Repeat interval after successful delivery, seconds |
| `SENSOR_EVENT_RETRY_S` | 60 | Retry interval after failed delivery, seconds |
| `SENSOR_EVENT_TIMEOUT_S` | 45 | Maximum wait for one network request, seconds |
| `SENSOR_STALE_US` | 10000000 | Reading expiry, microseconds |

Messages derive their thresholds and repeat intervals from the same settings. The trigger must be below 100, the rearm level must not exceed the trigger, and intervals must be positive. Rebuild and flash after changes; there is no runtime rule editor.

For temperature, light or other rules, add a policy in `events/` and call it from `sensor_events_tick()`. Obtain validated readings with `sensor_snapshot_find(snapshot, "channel_key")`, check units, then call `muse_event_sender_send()`. The sender accepts one request at a time: busy or offline returns false; an accepted request invokes one completion callback. Rules must retain pending reminders and manage repeat/retry intervals rather than send unconditionally on every sampling tick.

## Add a sensor

1. Add a driver under `drivers/`, following `sensor_driver_t`. `init()` receives the shared I2C bus; `read()` fills each channel's `sensor_value_t` and returns false on failure. Non-I2C drivers can ignore the bus and initialize verified pins themselves.
2. Define stable channel keys, short display names, units, types, an estimated-value flag and optional descriptions. Scalars use `axis[0]`; vectors use `axis[0..2]`. Channel counts must match `read()` output.
3. Register the driver in `k10_catalog.c` and add its source to the K10 branch of the parent `CMakeLists.txt`. Put shared sampling settings in `sensor_config.h`.
4. Build and verify values, units and validity. Muse `sensors.read` fields and descriptions automatically enumerate the registry, as do serial `readings`; no dispatch changes in `main/app.c` are needed.

Current capacities are 8 physical drivers, 16 channels and 4 channels per driver. If increasing them, check `SENSOR_TASK_STACK_BYTES` and `SENSOR_DESCRIPTION_BYTES`. Registration rejects duplicate keys, excess capacity and incomplete descriptions. Screen text comes from channel descriptions; many channels will require pagination on the 240x320 menu. Its current text buffer is 160 bytes.

## Interfaces and checks

- `>sensors`: retains original temperature, humidity, light and acceleration fields; `readings` lists all registered channels.
- `>sensors.muse`: prints the actual `sensors.read` result, including value/unit/age_s; invalid or expired readings are null.
- `>sensors.registry.selftest`: checks lookup, expiry boundaries and invalid states using local snapshots only.
- `>humidity.alert.selftest`: checks configured thresholds, repeat interval, rearm and retry decisions.
- `>humidity.alert.test`: sends an explicitly simulated reminder without changing real readings or production reminder timing.

Hardware references remain in driver comments. Board heat can affect temperature; light is estimated lux; acceleration remains signed 12-bit raw data rather than g.
