# K10 USB configuration page

A static Web Serial UI for configuring K10 directly. There is no backend API, and credentials are not sent over the network. Users need desktop Chrome/Edge, a USB data cable, matching public firmware and their own Muse Gadgets SDK Token.

## Usage

Distribute **`k10-usb-config.html`** as a standalone file. Its script and styles are embedded with no external module dependency. If local-file Web Serial access is blocked, serve it over HTTPS or localhost.

1. Open the standalone file, or deploy `index.html`, `style.css`, `app.mjs` and `serial-client.mjs` together to an HTTPS static host.
2. Boot K10 normally. BOOT download mode does not provide the configuration protocol; release BOOT and press RESET once after flashing. No valid-format SDK Token means automatic, indefinite USB setup. With an existing token, enter **USB mode** in K10's menu first. There is no five-second window.
3. Connect, enter an SDK Token and optional voice settings, then choose **Save all settings** at the bottom. The page validates all entered fields before writing, waits for each save acknowledgement, and requests restart only after all writes succeed. Blank Token preserves the existing token; blank Cartesia key preserves voice settings. Changing a saved voice/language requires entering the key again.
4. Enable Developer mode in the Muse app, add the device, configure Wi-Fi and pair.

For local hosting: `python -m http.server 8766 --bind 127.0.0.1`, then open `http://localhost:8766`. This is a temporary static server, not a service end users must install. The multi-file version uses ES modules and should not be opened directly as a local file. HTTPS hosts must serve `.mjs` as JavaScript. Open the page at the top level to avoid iframe serial-permission restrictions.

Maintain the four source files and run `python tools/muse/build_usb_config.py` to rebuild the standalone HTML. Public packaging rebuilds it automatically. Content hashes permit only the bundled inline script/style; network API requests remain blocked. The page uses no browser localStorage/cookies, third-party scripts or analytics.

Disconnection, timeout and storage failure never claim a confirmed save. If a write times out, check status before writing again. Physical USB access permits replacing or clearing settings without a device password, but the protocol cannot read credentials back. NVS encryption depends on device eFuses and firmware; this page does not configure irreversible eFuses.

## Operation semantics

- **Save all settings** uses existing `save` and `tts.save` requests followed by `restart`. This is not an atomic transaction across settings. If a later write fails, the page lists confirmed saves and stops before requesting restart. A failed restart acknowledgement reports the saved settings and asks the user to press RESET if needed.
- SDK Token storage is `homehub/sdk_token`; NVS commits are read back and checked. Muse pairing and Wi-Fi are preserved.
- Clearing only the SDK Token deletes that key and takes effect after restart; pairing, Wi-Fi and TTS are preserved.
- Full clearing removes the SDK Token, TTS settings, account pairing, Wi-Fi and manually entered Muse Token/VM, then restarts. Volume and brightness preferences remain. Use this before transferring a device.
- Pairing rollback, normal unpair and menu pairing reset preserve the SDK Token.
- Normal flash/OTA preserves NVS; erasing the entire flash removes settings.
- Older development firmware may fall back to an embedded token after clearing NVS. Public `GADGET_SDK_RUNTIME_ONLY=y` builds have no such fallback.

## USB protocol v1

Request: `>sdk.config={"id":"request_1","op":"status"}` followed by a newline. Operations: `status`, `save` (with `token`), `clear`, `restart`, `factory`. IDs contain 1-32 letters, digits, underscores or hyphens. Token validation matches SDK compile-time checks. Never put real credentials in documentation or shell history.

Responses begin with `@sdk.config ` and contain JSON with `v:1`, `runtime_only`, `id`, `ok`, `configured`, `active`, `restart_required`, `device` and `board`. Fixed errors include `invalid_request`, `invalid_token`, `busy` and `storage_error`. Responses never include the token or prefix fragments. The page ignores other device logs and matches request IDs. It permits configuration only on `runtime_only:true` K10 firmware, avoiding embedded-token fallback ambiguity.

The PSRAM-stack serial task parses and queues requests only. The internal-stack keeper performs NVS I/O under the pairing/reset operation gate. Connection work can hold this gate for about 20 seconds; page requests allow 45 seconds. The boot token cache is immutable so pairing pointers and VM API caches remain consistent.

USB mode is RAM-only. With no token, the normal animated avatar remains with `USB` / `Set SDK Token first.` in the header. Manual menu entry shows a locked USB page. Both states ignore physical buttons, persist across page disconnection, and end at restart. Startup then checks token presence/format again; Muse verifies cloud validity during pairing.

Normal operation creates no USB configuration task, request queue, driver buffers or interrupt. The keeper opens a VFS descriptor and creates the task/queue only on entry. No USB driver is installed even in setup mode. Failure closes the descriptor and releases the queue. RX reads the hardware FIFO; TX uses VFS with the log's shared lock. The page does not toggle DTR/RTS.

The shared Wi-Fi/NVS keeper still performs its normal work. Network and TTS remain available for voice testing. Hardware USB and startup logs may remain visible even when the configuration channel is off. ROM BOOT flashing is unaffected.

## Public build

With ESP-IDF 6.0.1, run `tools/muse/k10-public.ps1 -Action package`. The isolated directory is `build-muse-k10-public`, with `devices/sdkconfig.muse-k10-public` loaded last. Packaging includes only the four required flash images, the standalone configuration HTML, usage instructions, checksums and license notices. Multi-file page sources and operating-system-specific flash helpers are not included in the user download. It excludes sdkconfig, NVS, development logs and personal service settings, and rejects complete credential patterns. Output: `build-muse-k10-public/release`.

## Optional Cartesia PCM TTS

The page saves an API Key, Voice ID and language, effective after restart. Default behavior without a key is text-only. K10 connects directly through TLS WebSocket; the page neither contacts Cartesia nor stores keys. A test ACK means queued, not successful playback; refresh status to check the result. Synthesis consumes user credits. Full clearing removes TTS; ordinary pairing rollback preserves it. Physical buttons are ignored in USB mode.

See [streaming TTS](../../../components/muse/tts/README.md). Remove the old device in the Muse app before pairing again after clearing local settings.
