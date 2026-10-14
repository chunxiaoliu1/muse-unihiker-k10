#include "sdkconfig.h"
#include "tts_credentials.h"
#include "config_store.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#define TTS_CONFIG_KEY "cartesia_tts"
#define TTS_CONFIG_BYTES 512
static muse_tts_config_t s_active;
static void wipe(void *memory, size_t size) { volatile unsigned char *p = memory; while (size--) *p++ = 0; }
bool tts_credentials_valid(const muse_tts_config_t *c)
{
    size_t key = strnlen(c->api_key, sizeof(c->api_key));
    if (key < 16 || key >= sizeof(c->api_key) || strspn(c->api_key, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != key) return false;
    if (strnlen(c->voice, sizeof(c->voice)) != 36) return false;
    for (int i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (c->voice[i] != '-') return false; }
        else if (!strchr("0123456789abcdefABCDEF", c->voice[i])) return false;
    }
    return strnlen(c->language, sizeof(c->language)) == 2 && strspn(c->language, "abcdefghijklmnopqrstuvwxyz") == 2;
}
static bool load(muse_tts_config_t *out)
{
    memset(out, 0, sizeof(*out));
    config_key_lookup_t state = config_key_lookup(TTS_CONFIG_KEY);
    if (state == CONFIG_KEY_NOT_FOUND) return true;
    char encoded[TTS_CONFIG_BYTES] = {0};
    if (state != CONFIG_KEY_FOUND || !config_get_str(TTS_CONFIG_KEY, encoded, sizeof(encoded))) return false;
    cJSON *json = cJSON_Parse(encoded);
    const char *key = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json, "api_key"));
    const char *voice = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json, "voice"));
    const char *language = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json, "language"));
    bool ok = key && voice && language && strlen(key) < sizeof(out->api_key)
        && strlen(voice) < sizeof(out->voice) && strlen(language) < sizeof(out->language);
    if (ok) { strcpy(out->api_key, key); strcpy(out->voice, voice); strcpy(out->language, language); ok = tts_credentials_valid(out); }
    if (key) wipe((void *)key, strlen(key));
    cJSON_Delete(json); wipe(encoded, sizeof(encoded));
    if (!ok) wipe(out, sizeof(*out));
    return ok;
}
void tts_credentials_init(void)
{
    if (!load(&s_active)) wipe(&s_active, sizeof(s_active));
    muse_tts_configure(&s_active);
}
bool tts_credentials_status(bool *configured, bool *restart, muse_tts_config_t *stored)
{
    muse_tts_config_t config = {0};
    bool ok = load(&config);
    *configured = config.api_key[0] != '\0';
    *restart = memcmp(&config, &s_active, sizeof(config)) != 0;
    if (stored) *stored = config;
    wipe(&config, sizeof(config)); return ok;
}
bool tts_credentials_save(const muse_tts_config_t *config)
{
    if (!tts_credentials_valid(config)) return false;
    cJSON *json = cJSON_CreateObject();
    if (!json) return false;
    bool ok = cJSON_AddStringToObject(json, "api_key", config->api_key)
        && cJSON_AddStringToObject(json, "voice", config->voice)
        && cJSON_AddStringToObject(json, "language", config->language);
    char *encoded = ok ? cJSON_PrintUnformatted(json) : NULL;
    cJSON *key = cJSON_GetObjectItemCaseSensitive(json, "api_key");
    if (cJSON_IsString(key)) wipe(key->valuestring, strlen(key->valuestring));
    cJSON_Delete(json);
    ok = encoded && strlen(encoded) < TTS_CONFIG_BYTES && config_set_str(TTS_CONFIG_KEY, encoded);
    if (encoded) { wipe(encoded, strlen(encoded)); free(encoded); }
    muse_tts_config_t stored = {0};
    ok = ok && load(&stored) && memcmp(config, &stored, sizeof(stored)) == 0;
    wipe(&stored, sizeof(stored)); return ok;
}
bool tts_credentials_clear(void)
{
    return config_erase_key(TTS_CONFIG_KEY) && config_key_lookup(TTS_CONFIG_KEY) == CONFIG_KEY_NOT_FOUND;
}
