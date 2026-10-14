#include "sdkconfig.h"
#include "sdk_credentials.h"
#include "sdk_token_format.h"
#include "config_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char s_active[SDK_TOKEN_CAP];

/* Volatile stores prevent dead-store elimination of credential cleanup. */
static void wipe(void *memory, size_t size)
{
    volatile unsigned char *p = memory;
    while (size--) *p++ = 0;
}

#if CONFIG_MUSE_ENABLED
#include "app.h"
#include "identity.h"
#include "muse_board.h"
#include "muse_settings.h"
#if CONFIG_MUSE_K10_CARTESIA
#include "tts_credentials.h"
#include "muse_voice.h"
#include "muse_wifi.h"
#endif
#include "cJSON.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

typedef enum { SDK_STATUS, SDK_SAVE, SDK_CLEAR, SDK_RESTART, SDK_FACTORY, TTS_SAVE, TTS_CLEAR, TTS_TEST, TTS_SELFTEST } sdk_action_t;
typedef struct {
    char id[33];
    sdk_action_t action;
    char token[SDK_TOKEN_CAP];
#if CONFIG_MUSE_K10_CARTESIA
    muse_tts_config_t tts;
    bool tts_test_long;
#endif
} sdk_request_t;
static QueueHandle_t s_requests;

#if CONFIG_MUSE_BOARD_DFROBOT_UNIHIKER_K10
bool sdk_credentials_usb_start(void)
{
    if (!s_requests) s_requests = xQueueCreate(2, sizeof(sdk_request_t));
    return s_requests != NULL;
}

void sdk_credentials_usb_stop(void)
{
    // Only after transport startup failed: no producer has been created.
    if (s_requests) vQueueDelete(s_requests);
    s_requests = NULL;
}
#endif

static void reply(const char *id, const char *error, bool configured, bool restart, bool storage_safe)
{
    cJSON *json = cJSON_CreateObject();
    if (!json) return; /* Host times out and can query again; never invent an ACK. */
    cJSON_AddNumberToObject(json, "v", 1);
#if CONFIG_GADGET_SDK_RUNTIME_ONLY
    cJSON_AddBoolToObject(json, "runtime_only", true);
#else
    cJSON_AddBoolToObject(json, "runtime_only", false);
#endif
    cJSON_AddStringToObject(json, "id", id);
    cJSON_AddBoolToObject(json, "ok", error == NULL);
    if (error) cJSON_AddStringToObject(json, "error", error);
    cJSON_AddBoolToObject(json, "configured", configured);
    cJSON_AddBoolToObject(json, "active", s_active[0] != '\0');
    cJSON_AddBoolToObject(json, "restart_required", restart);
    cJSON_AddStringToObject(json, "device", identity_node_id());
    cJSON_AddStringToObject(json, "board", muse_board->name);
#if CONFIG_MUSE_K10_CARTESIA
    bool tts_configured = false, tts_restart = false;
    muse_tts_config_t tts = {0};
    bool tts_ok = storage_safe && tts_credentials_status(&tts_configured, &tts_restart, &tts);
    cJSON_AddBoolToObject(json, "tts_supported", true);
    cJSON_AddBoolToObject(json, "tts_configured", tts_ok && tts_configured);
    cJSON_AddBoolToObject(json, "tts_active", muse_tts_configured());
    cJSON_AddBoolToObject(json, "tts_restart_required", tts_restart);
    cJSON_AddBoolToObject(json, "tts_storage_ok", tts_ok);
    cJSON_AddStringToObject(json, "tts_voice", tts.voice);
    cJSON_AddStringToObject(json, "tts_language", tts.language);
    cJSON_AddStringToObject(json, "tts_state", muse_tts_last_state());
    cJSON_AddStringToObject(json, "tts_error", muse_tts_last_error());
    wipe(&tts, sizeof(tts));
#else
    cJSON_AddBoolToObject(json, "tts_supported", false);
#endif
    char *text = cJSON_PrintUnformatted(json);
    if (text) { printf("\n@sdk.config %s\n", text); fflush(stdout); }
    free(text);
    cJSON_Delete(json);
}

bool sdk_credentials_submit(const char *line, bool whole)
{
    sdk_request_t request = {0};
    const char *error = "invalid_request";
    cJSON *root = NULL;
    if (!whole || strncmp(line, "sdk.config=", 11)) goto done;
    const char *end = NULL;
    root = cJSON_ParseWithOpts(line + 11, &end, true);
    if (!cJSON_IsObject(root)) goto done;
    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    if (!cJSON_IsString(id) || !id->valuestring[0] || strlen(id->valuestring) > 32 ||
        strspn(id->valuestring, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")
            != strlen(id->valuestring)) goto done;
    strcpy(request.id, id->valuestring);
    cJSON *op = cJSON_GetObjectItemCaseSensitive(root, "op");
    if (!cJSON_IsString(op)) goto done;
    if (!strcmp(op->valuestring, "status")) request.action = SDK_STATUS;
    else if (!strcmp(op->valuestring, "clear")) request.action = SDK_CLEAR;
    else if (!strcmp(op->valuestring, "restart")) request.action = SDK_RESTART;
    else if (!strcmp(op->valuestring, "factory")) request.action = SDK_FACTORY;
    else if (!strcmp(op->valuestring, "save")) {
        request.action = SDK_SAVE;
        cJSON *token = cJSON_GetObjectItemCaseSensitive(root, "token");
        if (!cJSON_IsString(token) || !sdk_token_valid(token->valuestring)) {
            error = "invalid_token";
            goto done;
        }
        strcpy(request.token, token->valuestring);
    }
#if CONFIG_MUSE_K10_CARTESIA
    else if (!strcmp(op->valuestring, "tts.clear")) request.action = TTS_CLEAR;
    else if (!strcmp(op->valuestring, "tts.test")) {
        request.action = TTS_TEST;
        cJSON *long_test = cJSON_GetObjectItemCaseSensitive(root, "long");
        if (long_test && !cJSON_IsBool(long_test)) goto done;
        request.tts_test_long = cJSON_IsTrue(long_test);
    }
    else if (!strcmp(op->valuestring, "tts.selftest")) request.action = TTS_SELFTEST;
    else if (!strcmp(op->valuestring, "tts.save")) {
        request.action = TTS_SAVE;
        const char *key = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "api_key"));
        const char *voice = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "voice"));
        const char *language = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "language"));
        if (!voice || !voice[0]) voice = MUSE_TTS_DEFAULT_VOICE;
        if (!language || !language[0]) language = "en";
        if (!key || strlen(key) >= sizeof(request.tts.api_key) || strlen(voice) >= sizeof(request.tts.voice)
            || strlen(language) >= sizeof(request.tts.language)) { error = "invalid_tts"; goto done; }
        strcpy(request.tts.api_key, key); strcpy(request.tts.voice, voice); strcpy(request.tts.language, language);
        if (!tts_credentials_valid(&request.tts)) { error = "invalid_tts"; goto done; }
    }
#endif
    else goto done;
    error = (!s_requests || xQueueSend(s_requests, &request, 0) != pdTRUE) ? "busy" : NULL;
done:
    /* cJSON owns another copy of the secret. */
    if (root) {
        cJSON *token = cJSON_GetObjectItemCaseSensitive(root, "token");
        if (cJSON_IsString(token)) wipe(token->valuestring, strlen(token->valuestring));
        cJSON *key = cJSON_GetObjectItemCaseSensitive(root, "api_key");
        if (cJSON_IsString(key)) wipe(key->valuestring, strlen(key->valuestring));
    }
    cJSON_Delete(root);
    if (error) reply(request.id, error, false, false, false);
    wipe(&request, sizeof(request));
    return error == NULL;
}

static void process_one(void)
{
    sdk_request_t request;
    if (!s_requests || xQueueReceive(s_requests, &request, 0) != pdTRUE) return;
    if (!app_credentials_begin()) {
        reply(request.id, "busy", false, false, false);
        wipe(&request, sizeof(request));
        return;
    }
    const char *error = NULL;
    bool reboot = request.action == SDK_RESTART;
    if (request.action == SDK_SAVE && !config_set_str(SDK_TOKEN_KEY, request.token)) error = "storage_error";
    if (request.action == SDK_CLEAR && !config_erase_key(SDK_TOKEN_KEY)) error = "storage_error";
#if CONFIG_MUSE_K10_CARTESIA
    if (request.action == TTS_SAVE && !tts_credentials_save(&request.tts)) error = "storage_error";
    if (request.action == TTS_CLEAR && !tts_credentials_clear()) error = "storage_error";
    if (request.action == TTS_SELFTEST) {
        const char *state = muse_tts_last_state();
        if (!strcmp(state, "playing") || !strcmp(state, "buffering") || !strcmp(state, "connecting")) error = "busy";
        else if (!muse_tts_stream_selftest()) error = "tts_selftest_failed";
    }
    if (request.action == TTS_TEST) {
        bool configured = false, restart = false;
        if (!tts_credentials_status(&configured, &restart, NULL)) error = "storage_error";
        else if (restart) error = "restart_required";
        else if (!muse_tts_configured()) error = "tts_not_configured";
        else if (!muse_wifi_connected()) error = "wifi_offline";
        else if (!muse_settings_speaker_on()) error = "speaker_off";
        else if (!muse_voice_request_ttstest(request.tts_test_long)) error = "busy";
    }
#endif
    if (request.action == SDK_FACTORY) {
        bool cleared = config_clear_setup();
        if (!config_erase_key(SDK_TOKEN_KEY)) cleared = false;
        if (!muse_settings_clear_credentials()) cleared = false;
#if CONFIG_MUSE_K10_CARTESIA
        if (!tts_credentials_clear()) cleared = false;
#endif
        if (!cleared) error = "storage_error";
        else reboot = true;
    }
    char stored[SDK_TOKEN_CAP] = {0};
    config_key_lookup_t state = config_key_lookup(SDK_TOKEN_KEY);
    if (state == CONFIG_KEY_LOOKUP_ERROR ||
        (state == CONFIG_KEY_FOUND && !config_get_str(SDK_TOKEN_KEY, stored, sizeof(stored)))) error = "storage_error";
    if (request.action == SDK_SAVE && strcmp(stored, request.token)) error = "storage_error";
    if ((request.action == SDK_CLEAR || request.action == SDK_FACTORY) && state != CONFIG_KEY_NOT_FOUND) error = "storage_error";
    bool configured = sdk_token_valid(stored);
#if !CONFIG_GADGET_SDK_RUNTIME_ONLY
    if (state == CONFIG_KEY_NOT_FOUND && sdk_token_valid(CONFIG_GADGET_SDK_TOKEN))
        strcpy(stored, CONFIG_GADGET_SDK_TOKEN);
    configured = sdk_token_valid(stored);
#endif
    reply(request.id, error, configured, strcmp(stored, s_active) != 0, true);
    wipe(stored, sizeof(stored));
    wipe(&request, sizeof(request));
    if (!error && reboot) {
        /* Hold setup gate through reboot, so pairing cannot recreate wiped keys. */
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
    app_credentials_end();
}

void sdk_credentials_process(void)
{
    while (s_requests && uxQueueMessagesWaiting(s_requests)) process_one();
}
#endif

void sdk_credentials_init(void)
{
    wipe(s_active, sizeof(s_active));
    /* A corrupt/oversized NVS entry must never fall back to another user's key. */
    config_key_lookup_t state = config_key_lookup(SDK_TOKEN_KEY);
    if (state == CONFIG_KEY_FOUND) {
        if (!config_get_str(SDK_TOKEN_KEY, s_active, sizeof(s_active)) || !sdk_token_valid(s_active))
            wipe(s_active, sizeof(s_active));
    }
#if !CONFIG_GADGET_SDK_RUNTIME_ONLY
    else if (state == CONFIG_KEY_NOT_FOUND && sdk_token_valid(CONFIG_GADGET_SDK_TOKEN))
        strcpy(s_active, CONFIG_GADGET_SDK_TOKEN);
#endif
#if CONFIG_MUSE_ENABLED
#if CONFIG_MUSE_K10_CARTESIA
    tts_credentials_init();
#endif
#if !CONFIG_MUSE_BOARD_DFROBOT_UNIHIKER_K10
    s_requests = xQueueCreate(2, sizeof(sdk_request_t));
#endif
#endif
}

const char *sdk_credentials_active(void) { return s_active[0] ? s_active : NULL; }
