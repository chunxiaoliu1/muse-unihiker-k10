#pragma once
#include "tts/tts_stream.h"
void tts_credentials_init(void);
bool tts_credentials_save(const muse_tts_config_t *config);
bool tts_credentials_clear(void);
/* All storage calls must run on the internal-stack keeper. */
bool tts_credentials_status(bool *configured, bool *restart, muse_tts_config_t *stored);
bool tts_credentials_valid(const muse_tts_config_t *config);
