# Cartesia PCM streaming speech synthesis

K10 connects directly to Cartesia over a TLS WebSocket. Each complete Muse reply is synthesized once and received as continuous 16 kHz mono, 16-bit little-endian PCM, without MP3 decoding. No user API Key means text-only replies; there is no computer-side service or embedded developer key.

ESP-IDF `tcp_transport` runs TLS, send/receive, parsing and cleanup in one PSRAM-stack worker, avoiding an extra WebSocket task with an 8 KiB internal stack. Cancellation sets a flag; the connection owner releases the connection and its stream reference after reading stops. Text and receive buffers are explicitly allocated in PSRAM. Full-buffer waits share the overall request deadline. `tts.stream` logs only stack low-water marks and internal RAM; startup logs include `reset_reason`.

ESP-IDF 6.0.1 `vStreamBufferDeleteWithCaps` incorrectly passes a stream buffer to the queue deletion API. This module gets both allocations with `xStreamBufferGetStaticBuffers`, calls `vStreamBufferDelete`, then frees each allocation once, without changing the system SDK. The WithCaps creation API follows static-buffer rules and reserves one byte, so allocation adds one byte. Reads consume only complete 16-bit samples, including when the producer has temporarily supplied a single byte.

- `tts_stream.h/.cpp`: stream objects, WebSocket transport, fragmented JSON, Base64 PCM, cancellation and bounded buffering.
- `tts_config.h`: fixed model/API version, prebuffer size, capacity and deadlines.
- `main/credentials/tts_credentials.c`: one NVS configuration record, save/readback checks, boot cache and clearing.
- `tools/muse/usb-config/`: standalone HTML and multi-file USB configuration UI.

Get a regular user key at https://play.cartesia.ai/keys. Save the key, Voice ID and language through the page, then restart. Changes preserve Muse pairing and Wi-Fi. Clearing only the SDK Token or rolling back pairing preserves TTS; clearing all user settings also clears TTS. Before pairing from scratch, remove the old device in the Muse app.

Credentials live in one `homehub/cartesia_tts` NVS JSON string; the boot cache is immutable. Serial/PSRAM-stack code validates and queues requests; the internal-stack keeper performs NVS I/O under the operation gate. Responses expose only configured status, Voice ID, language and fixed diagnostic states. They never return the key or log raw provider errors. NVS is currently unencrypted; physical USB access permits replacing settings. Do not distribute device NVS or a publisher's shared API Key.

Playback starts after 32,000 bytes (about one second). The receive buffer is limited to 256 KiB (about eight seconds). A full buffer waits instead of dropping samples; underruns trigger prebuffering again. Short completed streams drain the remaining PCM directly. Network, provider and quota errors preserve text replies; network interruptions may still pause audio.

Consumer/worker references keep each stream alive. Cancellation applies only to that request, and stale responses cannot enter new playback. Automatic reconnect/synthesis retry is disabled to avoid duplicate charges. HTTP/WS requests use only the fixed Cartesia domain with CA verification.

## USB protocol

Requests use `>sdk.config={...}` and correlated `@sdk.config` responses:

- `tts.save`: `api_key`, `voice` (UUID; default when empty), and `language` (two-letter code).
- `tts.clear`: clear TTS settings only; restart applies the change.
- `tts.test`: queue a fixed voice test while online, speaker enabled and idle. ACK means queued, not successful synthesis or playback. It uses the user's credits. Optional `"long":true` selects a long diagnostic passage; the page uses the short test.
- `tts.selftest`: actual PCM/parser/buffer checks with no network, key or audio output. Includes fragmented WebSocket frames, short-tail draining, rebuffering and full-buffer timeout.
- `status`: `tts_supported/configured/active/restart_required/storage_ok/voice/language/state/error`.

A cancels playback in normal operation; physical buttons are ignored in USB configuration mode.

States: `idle/connecting/buffering/playing/done/error`. Errors: `network/timeout/service/protocol/memory`. Provider errors include invalid keys, insufficient credits and invalid parameters; check the user's Cartesia console for details.

The model is `sonic-3.6`, API version `2026-08-14`. New setup defaults to English; previously saved language settings remain in effect. Choose a voice in Cartesia Playground and copy its Voice ID. The default UUID comes from an official example; preview a suitable voice for your language. Built-in short and long test passages are English. Actual replies use the selected synthesis language; multilingual display support remains available.

Official protocol: https://docs.cartesia.ai/api-reference/tts/websocket

For intermittent restart diagnostics, disconnect the web page's serial connection and run:

```powershell
python tools/muse/tts_diagnostics.py COM10 --seconds 600 --output tts-diagnostic.log
```

The tool does not reset K10 and saves only selected memory, stack, boot and crash metadata, never chat text or credentials. `--test --long` triggers long playback and uses your Cartesia credits. Enter USB mode first when settings are already configured.
