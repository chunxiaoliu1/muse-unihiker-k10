#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define MUSE_TTS_KEY_CAP 256
#define MUSE_TTS_VOICE_CAP 37
#define MUSE_TTS_LANGUAGE_CAP 8
#define MUSE_TTS_DEFAULT_VOICE "a0e99841-438c-4a64-b679-ae501e7d6091"
#define MUSE_TTS_RATE 16000
typedef struct {
    char api_key[MUSE_TTS_KEY_CAP];
    char voice[MUSE_TTS_VOICE_CAP];
    char language[MUSE_TTS_LANGUAGE_CAP];
} muse_tts_config_t;
typedef struct muse_tts_stream muse_tts_stream_t;
typedef bool (*muse_tts_current_fn)(uint32_t generation);
typedef struct {
    size_t frames;
    bool ended;
    bool failed;
} muse_tts_update_t;

/* Boot-only immutable credentials; no NVS access from network/audio tasks. */
void muse_tts_configure(const muse_tts_config_t *config);
bool muse_tts_configured(void);
const char *muse_tts_last_state(void);
const char *muse_tts_last_error(void);
/* Each stream owns its buffers; cancellation never resets another request. */
muse_tts_stream_t *muse_tts_stream_open(const char *text, uint32_t generation, muse_tts_current_fn current);
muse_tts_update_t muse_tts_stream_read(muse_tts_stream_t *stream, int16_t *pcm, size_t frames);
void muse_tts_stream_close(muse_tts_stream_t *stream);
/* Deterministic board test: no network, no credentials, no speaker output. */
bool muse_tts_stream_selftest(void);
#ifdef __cplusplus
}
#endif
