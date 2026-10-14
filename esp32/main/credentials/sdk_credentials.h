#pragma once
#include <stdbool.h>

/* Call at boot on an internal stack, after config_store_init(). The active
 * token is immutable until restart: pairing retains its pointer. */
void sdk_credentials_init(void);
const char *sdk_credentials_active(void);

#if CONFIG_MUSE_ENABLED
/* USB handler copies the request; the keeper performs NVS I/O on its
 * internal stack. Neither requests nor responses ever echo credentials. */
bool sdk_credentials_submit(const char *line, bool whole);
void sdk_credentials_process(void);
#if CONFIG_MUSE_BOARD_DFROBOT_UNIHIKER_K10
/* Keeper-owned: allocate only on entry; stop rolls back a failed entry. */
bool sdk_credentials_usb_start(void);
void sdk_credentials_usb_stop(void);
#endif
#endif
