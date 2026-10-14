#include "tts_stream.h"
#include "tts_config.h"
#include "sdkconfig.h"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <new>

#if CONFIG_MUSE_K10_CARTESIA
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_transport_ssl.h"
#include "esp_transport_ws.h"
#include "mbedtls/base64.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"

enum state_t { IDLE, CONNECTING, BUFFERING, PLAYING, DONE, ERROR };
enum tts_error_t { NO_ERROR, NETWORK, TIMEOUT, SERVICE, PROTOCOL, NO_MEMORY };
static std::atomic<state_t> s_state{IDLE};
static std::atomic<tts_error_t> s_error{NO_ERROR};
static muse_tts_config_t s_config;
static QueueHandle_t s_requests;

struct muse_tts_stream {
    std::atomic<unsigned> refs{2}; // consumer and worker
    std::atomic<bool> cancelled{false}, ended{false}, failed{false};
    std::atomic<int64_t> last_rx{0};
    int64_t deadline_us = 0; // producer only; bounds waits on a full PCM buffer
    StreamBufferHandle_t audio;
    uint32_t generation;
    muse_tts_current_fn current;
    char *text;
    char context[48];
    char *message = nullptr;
    size_t message_len = 0, message_cap = 0, frame_received = 0;
    bool released_audio = false; // consumer only
};

static bool alive(muse_tts_stream_t *s)
{
    return !s->cancelled.load() && s->current(s->generation);
}
static void delete_audio(StreamBufferHandle_t audio)
{
    if (!audio) return;
    uint8_t *storage = nullptr;
    StaticStreamBuffer_t *control = nullptr;
    if (xStreamBufferGetStaticBuffers(audio, &storage, &control) != pdTRUE) {
        ESP_LOGE("tts.stream", "unexpected non-static audio buffer");
        return;
    }
    // ESP-IDF 6.0.1's vStreamBufferDeleteWithCaps incorrectly calls
    // vSemaphoreDelete (queue layout), then frees the stream control again.
    // Use the stream API for the static object and free its saved allocations.
    vStreamBufferDelete(audio);
    heap_caps_free(control);
    heap_caps_free(storage);
}
static void release(muse_tts_stream_t *s)
{
    if (s->refs.fetch_sub(1) == 1) {
        delete_audio(s->audio);
        free(s->text); free(s->message); delete s;
    }
}
static void fail(muse_tts_stream_t *s, tts_error_t error)
{
    if (alive(s) && !s->ended.load()) {
        s_error.store(error); s_state.store(ERROR);
        s->failed.store(true); s->ended.store(true);
    }
}
static void parse_message(muse_tts_stream_t *s)
{
    s->message[s->message_len] = '\0';
    cJSON *json = cJSON_ParseWithLength(s->message, s->message_len + 1);
    if (!json) { fail(s, PROTOCOL); return; }
    const char *type = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json, "type"));
    const char *context = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json, "context_id"));
    if (!type || !context || strcmp(context, s->context)) {
        fail(s, PROTOCOL);
    } else if (!strcmp(type, "error")) {
        // Never log provider responses: they may echo text or credentials.
        fail(s, SERVICE);
    } else if (!strcmp(type, "chunk")) {
        const char *data = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json, "data"));
        size_t bytes = 0;
        if (!data) fail(s, PROTOCOL);
        else {
            size_t encoded = strlen(data);
            int rc = mbedtls_base64_decode(nullptr, 0, &bytes, (const unsigned char *)data, encoded);
            if ((rc != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL && rc != 0) || (bytes & 1) || bytes > MUSE_TTS_MESSAGE_MAX_BYTES) fail(s, PROTOCOL);
            else if (bytes) {
                auto pcm = static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
                if (!pcm) fail(s, NO_MEMORY);
                else {
                    if (mbedtls_base64_decode(pcm, bytes, &bytes, (const unsigned char *)data, encoded) != 0 || (bytes & 1)) fail(s, PROTOCOL);
                    else {
                        size_t off = 0;
                        while (off < bytes && alive(s) && !s->ended.load()) {
                            off += xStreamBufferSend(s->audio, pcm + off, bytes - off, pdMS_TO_TICKS(50));
                            if (s->deadline_us && esp_timer_get_time() > s->deadline_us) fail(s, TIMEOUT);
                        }
                    }
                    free(pcm);
                }
            }
        }
    } else if (!strcmp(type, "done")) {
        s->ended.store(true);
    } else {
        fail(s, PROTOCOL); // timestamps and flush events are not requested
    }
    cJSON_Delete(json);
}

static void receive_fragment(muse_tts_stream_t *s, int opcode, bool fin,
                             const char *data, int length, int payload_length, int offset)
{
    if (!alive(s) || s->ended.load()) return;
    s->last_rx.store(esp_timer_get_time());
    if (opcode != 0 && opcode != 1) { fail(s, PROTOCOL); return; }
    if (length < 0 || payload_length < 0 || offset < 0
        || (size_t)payload_length > MUSE_TTS_MESSAGE_MAX_BYTES) { fail(s, PROTOCOL); return; }
    if (offset == 0) {
        s->frame_received = 0;
        if ((opcode == 1 && s->message_len) || (opcode == 0 && !s->message_len)) {
            fail(s, PROTOCOL); return;
        }
    }
    if (offset > payload_length || (size_t)offset != s->frame_received || length > payload_length - offset) {
        fail(s, PROTOCOL); return;
    }
    size_t want = s->message_len + length;
    if (want > MUSE_TTS_MESSAGE_MAX_BYTES) { fail(s, PROTOCOL); return; }
    if (want + 1 > s->message_cap) {
        size_t cap = want + 4096;
        auto memory = static_cast<char *>(heap_caps_realloc(s->message, cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!memory) { fail(s, NO_MEMORY); return; }
        s->message = memory; s->message_cap = cap;
    }
    if (length) memcpy(s->message + s->message_len, data, length);
    s->message_len = want; s->frame_received += length;
    if (fin && s->frame_received == (size_t)payload_length) {
        parse_message(s); s->message_len = 0;
    }
}

static char *request_json(muse_tts_stream_t *s)
{
    cJSON *json = cJSON_CreateObject();
    if (!json) return nullptr;
    bool ok = cJSON_AddStringToObject(json, "model_id", MUSE_TTS_MODEL)
        && cJSON_AddStringToObject(json, "transcript", s->text)
        && cJSON_AddStringToObject(json, "voice", s_config.voice)
        && cJSON_AddStringToObject(json, "context_id", s->context)
        && cJSON_AddStringToObject(json, "language", s_config.language)
        && cJSON_AddBoolToObject(json, "continue", false)
        && cJSON_AddBoolToObject(json, "add_timestamps", false);
    cJSON *format = cJSON_AddObjectToObject(json, "output_format");
    ok = ok && format && cJSON_AddStringToObject(format, "container", "raw")
        && cJSON_AddStringToObject(format, "encoding", "pcm_s16le")
        && cJSON_AddNumberToObject(format, "sample_rate", MUSE_TTS_RATE);
    char *body = ok ? cJSON_PrintUnformatted(json) : nullptr;
    cJSON_Delete(json); return body;
}

static void worker(void *)
{
    muse_tts_stream_t *s;
    for (;;) {
        xQueueReceive(s_requests, &s, portMAX_DELAY);
        if (!alive(s)) { release(s); continue; }
        s_state.store(CONNECTING); s_error.store(NO_ERROR);
        // This PSRAM-stack task alone owns TLS/WS, including all teardown.
        // Avoid an additional 8 KiB internal-stack task and cross-task destroy.
        auto tls = esp_transport_ssl_init();
        auto ws = tls ? esp_transport_ws_init(tls) : nullptr;
        auto rx = static_cast<char *>(heap_caps_malloc(4096, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        char headers[MUSE_TTS_KEY_CAP + 64];
        snprintf(headers, sizeof(headers), "X-API-Key: %s\r\n", s_config.api_key);
        esp_transport_ws_config_t config = {};
        config.ws_path = "/tts/websocket?cartesia_version=" MUSE_TTS_API_VERSION;
        config.headers = headers;
        config.propagate_control_frames = false; // transport answers PING and handles CLOSE
        if (tls) esp_transport_ssl_crt_bundle_attach(tls, esp_crt_bundle_attach);
        bool configured = ws && esp_transport_ws_set_config(ws, &config) == ESP_OK;
        memset(headers, 0, sizeof(headers));
        int64_t start = esp_timer_get_time(); s->last_rx.store(start);
        s->deadline_us = start + MUSE_TTS_TOTAL_TIMEOUT_MS * 1000LL;
        char *body = request_json(s);
        if (!configured || !rx || !body) fail(s, NO_MEMORY);
        else if (alive(s)) {
            if (esp_transport_connect(ws, "api.cartesia.ai", 443, 10000) < 0) fail(s, NETWORK);
            else if (alive(s)) {
                size_t length = strlen(body);
                if (esp_transport_ws_send_raw(ws, static_cast<ws_transport_opcodes_t>(0x81),
                                             body, length, 10000) != (int)length) fail(s, NETWORK);
                else s_state.store(BUFFERING);
            }
        }
        uint32_t stack_min = UINT32_MAX;
        int64_t next_sample = 0;
        int offset = 0, payload = 0;
        while (alive(s) && !s->ended.load()) {
            int64_t now = esp_timer_get_time();
            if (now - s->last_rx.load() > MUSE_TTS_IDLE_TIMEOUT_MS * 1000LL
                || now > s->deadline_us) { fail(s, TIMEOUT); break; }
            if (now >= next_sample) {
                uint32_t free_stack = uxTaskGetStackHighWaterMark(nullptr);
                if (free_stack < stack_min) {
                    stack_min = free_stack;
                    ESP_LOGI("tts.stream", "stack_free=%lu internal_free=%u largest=%u",
                        (unsigned long)free_stack,
                        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
                }
                next_sample = now + 1000000;
            }
            int ready = esp_transport_poll_read(ws, 100);
            if (ready < 0) { fail(s, NETWORK); break; }
            if (!ready) continue;
            int received = esp_transport_read(ws, rx, 4096, 1000);
            if (received < 0) { fail(s, NETWORK); break; }
            if (!received) continue; // timeout or control frame handled by transport
            if (!offset) payload = esp_transport_ws_get_read_payload_len(ws);
            receive_fragment(s, esp_transport_ws_get_read_opcode(ws),
                             esp_transport_ws_get_fin_flag(ws), rx, received, payload, offset);
            offset += received;
            if (offset == payload) offset = 0;
        }
        // Closing the socket cancels an unfinished context without another send
        // that could block behind the PCM producer. No automatic retry/billing.
        if (tls) esp_transport_close(tls);
        if (ws) esp_transport_destroy(ws);
        if (tls) esp_transport_destroy(tls);
        free(rx);
        free(body); release(s);
    }
}

void muse_tts_configure(const muse_tts_config_t *config)
{
    if (config) s_config = *config;
    if (!s_config.api_key[0]) return;
    s_requests = xQueueCreate(MUSE_TTS_REQUEST_QUEUE_LENGTH, sizeof(muse_tts_stream_t *));
    if (!s_requests || xTaskCreateWithCaps(worker, "cartesia_tts", MUSE_TTS_WORKER_STACK_BYTES,
          nullptr, 3, nullptr, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        if (s_requests) vQueueDelete(s_requests);
        s_requests = nullptr; s_error.store(NO_MEMORY); s_state.store(ERROR);
    }
}
bool muse_tts_configured(void) { return s_requests && s_config.api_key[0]; }
const char *muse_tts_last_state(void)
{
    static const char *names[] = {"idle", "connecting", "buffering", "playing", "done", "error"};
    return names[s_state.load()];
}
const char *muse_tts_last_error(void)
{
    static const char *names[] = {"", "network", "timeout", "service", "protocol", "memory"};
    return names[s_error.load()];
}
muse_tts_stream_t *muse_tts_stream_open(const char *text, uint32_t generation, muse_tts_current_fn current)
{
    if (!muse_tts_configured() || !text || !text[0] || !current || !current(generation)) return nullptr;
    auto s = new(std::nothrow) muse_tts_stream_t;
    if (!s) return nullptr;
    // WithCaps uses static creation: one storage byte is reserved by FreeRTOS.
    // Keep the usable capacity even for 16-bit PCM samples.
    s->audio = xStreamBufferCreateWithCaps(MUSE_TTS_BUFFER_BYTES + 1, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    size_t text_bytes = strlen(text) + 1;
    s->text = static_cast<char *>(heap_caps_malloc(text_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (s->text) memcpy(s->text, text, text_bytes);
    s->generation = generation; s->current = current;
    snprintf(s->context, sizeof(s->context), "k10-%lu-%lld", (unsigned long)generation, (long long)esp_timer_get_time());
    if (!s->audio || !s->text || xQueueSend(s_requests, &s, 0) != pdTRUE) {
        delete_audio(s->audio);
        free(s->text); delete s; return nullptr;
    }
    return s;
}
muse_tts_update_t muse_tts_stream_read(muse_tts_stream_t *s, int16_t *pcm, size_t frames)
{
    muse_tts_update_t result = {};
    if (!s || !alive(s)) return result;
    bool ended = s->ended.load();
    size_t buffered = xStreamBufferBytesAvailable(s->audio);
    if (!s->released_audio && (buffered >= MUSE_TTS_PREBUFFER_BYTES || ended)) s->released_audio = true;
    if (s->released_audio && pcm && frames) {
        // After an underrun, accumulate the prebuffer again before resuming.
        if (!buffered && !ended) s->released_audio = false;
        else {
            size_t ready_frames = buffered / sizeof(int16_t);
            size_t take = frames < ready_frames ? frames : ready_frames;
            if (take) result.frames = xStreamBufferReceive(s->audio, pcm, take * sizeof(int16_t), 0) / sizeof(int16_t);
            if (result.frames) s_state.store(PLAYING);
        }
    }
    if (ended && !xStreamBufferBytesAvailable(s->audio)) {
        result.ended = true; result.failed = s->failed.load();
        s_state.store(result.failed ? ERROR : DONE);
    }
    return result;
}
void muse_tts_stream_close(muse_tts_stream_t *s)
{
    if (s) { s->cancelled.store(true); release(s); }
}

static bool selftest_current(uint32_t) { return true; }
bool muse_tts_stream_selftest(void)
{
    // Exercise the actual parser and stream buffer, including fragmented events.
    state_t previous_state = s_state.load(); tts_error_t previous_error = s_error.load();
    muse_tts_stream_t s;
    s.audio = xStreamBufferCreateWithCaps(MUSE_TTS_BUFFER_BYTES + 1, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s.current = selftest_current; s.generation = 0; s.text = nullptr;
    strcpy(s.context, "selftest");
    if (!s.audio) return false;
    bool ok = true;
    auto feed = [&](const char *text) {
        size_t length = strlen(text), split = length / 2;
        receive_fragment(&s, 1, true, text, split, length, 0);
        receive_fragment(&s, 1, true, text + split, length - split, length, split);
    };
    auto pcm_chunk = [&](size_t bytes) {
        auto pcm = static_cast<unsigned char *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        size_t capacity = 4 * ((bytes + 2) / 3) + 1;
        auto encoded = static_cast<unsigned char *>(heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!pcm || !encoded) { free(pcm); free(encoded); ok = false; return; }
        for (size_t i = 0; i < bytes; i += 2) { pcm[i] = 0x34; pcm[i+1] = 0x12; }
        size_t size = 0;
        if (mbedtls_base64_encode(encoded, capacity, &size, pcm, bytes) != 0) ok = false;
        encoded[size] = 0;
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "type", "chunk");
        cJSON_AddStringToObject(json, "context_id", "selftest");
        cJSON_AddStringToObject(json, "data", (const char *)encoded);
        char *text = cJSON_PrintUnformatted(json);
        if (text) feed(text); else ok = false;
        free(text); cJSON_Delete(json); free(pcm); free(encoded);
    };
    int16_t output[160];
    pcm_chunk(640);
    ok = ok && muse_tts_stream_read(&s, output, 160).frames == 0;
    pcm_chunk(MUSE_TTS_PREBUFFER_BYTES);
    size_t frames = 0;
    for (;;) {
        auto update = muse_tts_stream_read(&s, output, 160);
        if (!update.frames) break;
        for (size_t i = 0; i < update.frames; i++) if (output[i] != 0x1234) ok = false;
        frames += update.frames;
    }
    ok = ok && frames == (640 + MUSE_TTS_PREBUFFER_BYTES) / 2;
    pcm_chunk(640);
    ok = ok && muse_tts_stream_read(&s, output, 160).frames == 0; // rebuffer
    const char *done = "{\"type\":\"done\",\"context_id\":\"selftest\"}";
    size_t done_length = strlen(done), split = done_length / 2;
    // A JSON message fragmented across TEXT and CONTINUATION WS frames.
    receive_fragment(&s, 1, false, done, split, split, 0);
    receive_fragment(&s, 0, true, done + split, done_length - split, done_length - split, 0);
    auto first = muse_tts_stream_read(&s, output, 160);
    auto last = muse_tts_stream_read(&s, output, 160);
    ok = ok && first.frames == 160 && !first.ended && last.frames == 160 && last.ended && !last.failed;
    s.ended.store(false); s.released_audio = false;
    // A stalled consumer must not trap the producer forever with a full ring.
    pcm_chunk(MUSE_TTS_BUFFER_BYTES);
    s.deadline_us = esp_timer_get_time() - 1;
    pcm_chunk(640);
    ok = ok && s.failed.load() && s.ended.load() && s_error.load() == TIMEOUT;
    s.deadline_us = 0;
    while (xStreamBufferReceive(s.audio, output, sizeof(output), 0)) {}
    s.ended.store(false); s.failed.store(false);
    // A partial producer write must never discard an odd trailing PCM byte.
    s.released_audio = true;
    unsigned char partial[] = {0x34, 0x12};
    xStreamBufferSend(s.audio, partial, 1, 0);
    ok = ok && muse_tts_stream_read(&s, output, 1).frames == 0;
    xStreamBufferSend(s.audio, partial + 1, 1, 0);
    auto paired = muse_tts_stream_read(&s, output, 1);
    ok = ok && paired.frames == 1 && output[0] == 0x1234;
    feed("{\"type\":\"chunk\",\"context_id\":\"wrong-context\",\"data\":\"NBI=\"}");
    auto rejected = muse_tts_stream_read(&s, output, 160);
    ok = ok && rejected.failed && rejected.ended;
    s.cancelled.store(true);
    ok = ok && muse_tts_stream_read(&s, output, 160).frames == 0;
    delete_audio(s.audio); free(s.message);
    // Repeated teardown exercises the SDK workaround without a billable call.
    for (unsigned i = 0; i < 32; ++i) {
        auto audio = xStreamBufferCreateWithCaps(129 + i * 2, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!audio) { ok = false; break; }
        delete_audio(audio);
    }
    ok = heap_caps_check_integrity(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, false) && ok;
    s_state.store(previous_state); s_error.store(previous_error);
    return ok;
}
#else
void muse_tts_configure(const muse_tts_config_t *) {}
bool muse_tts_configured(void) { return false; }
const char *muse_tts_last_state(void) { return "disabled"; }
const char *muse_tts_last_error(void) { return ""; }
muse_tts_stream_t *muse_tts_stream_open(const char *, uint32_t, muse_tts_current_fn) { return nullptr; }
muse_tts_update_t muse_tts_stream_read(muse_tts_stream_t *, int16_t *, size_t) { return {}; }
void muse_tts_stream_close(muse_tts_stream_t *) {}
bool muse_tts_stream_selftest(void) { return false; }
#endif
