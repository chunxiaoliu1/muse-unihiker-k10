# Muse for UNIHIKER K10

Connect DFRobot UNIHIKER K10 to Muse with voice input, animated replies, sensors, on-demand photos and optional streaming speech.

This community project extends [Meta's Muse Gadgets SDK](https://github.com/facebookincubator/muse-gadget-sdk) for K10. It is not an official DFRobot or Meta firmware release. Original SDK documentation is preserved in [README_UPSTREAM.md](README_UPSTREAM.md).

## What K10 can do

- Pair with the Muse app and connect to Wi-Fi.
- Hold A to speak; see replies on the display with an animated avatar and RGB status lights.
- Read temperature, humidity, ambient light and three-axis acceleration through Muse's `sensors.read` command.
- Proactively report humidity above 70% RH, repeat every 15 minutes while high, and rearm at 65% RH or below.
- Capture a photo when Muse requests `camera.capture`, for questions such as "What am I looking at?"
- Read replies aloud using optional Cartesia PCM streaming, directly from K10. No computer-side speech service is needed.
- Configure your own SDK Token and optional Cartesia key using one standalone HTML file over USB.

The UI, project guides, diagnostics and built-in test passages are in English. Multilingual replies and CJK display support remain available.

## Hardware and setup

Requires UNIHIKER K10 with ESP32-S3, 16 MB flash and 8 MB PSRAM, a USB data cable, desktop Chrome/Edge, and the Muse app with Developer mode. Each user supplies their own SDK Token from [Muse Gadgets](https://gadgets.muse.ai). Voice replies additionally require a personal [Cartesia API Key](https://play.cartesia.ai/keys).

1. Download the public firmware package from this repository's Releases.
2. Follow the [flash and setup guide](esp32/tools/muse/K10_PUBLIC_RELEASE.md). Flashing uses four images at their specified addresses; the application BIN alone is not a complete image for address 0x0.
3. Open the included `usb-config/k10-usb-config.html` in Chrome or Edge.
4. With no token configured, K10 keeps its animated avatar and displays `USB` / `Set SDK Token first.`. Already configured devices must enter **USB mode** from the menu before connecting.
5. Enter the SDK Token and optional voice settings, then choose **Save all settings**. K10 restarts after every entered setting is confirmed saved.
6. Enable Developer mode in the Muse app, add K10, and set up Wi-Fi. Remove an old app device entry before pairing again after a full local reset.

Blank credentials preserve existing settings. Changing a stored voice or language requires entering the Cartesia key again. USB mode stays active until restart; physical buttons are ignored while configuring, but background networking and voice services continue. Normal startup with a token allocates no dedicated USB configuration task, queue or driver buffers.

No SDK Token, Cartesia key, Wi-Fi password or device NVS is included. The configuration page does not upload or read back credentials. Enabling speech sends reply text to Cartesia and consumes the user's credits.

## Build from source

Use **ESP-IDF v6.0.1**. From `esp32/`, with IDF activated:

```powershell
./tools/muse/k10-public.ps1 -Action build
```

The public profile is runtime-token-only, so it needs no developer token at build time. The audited release package is written to `esp32/build-muse-k10-public/release/`. Windows helpers use the configured IDF environment, or the installation at `C:\Espressif\frameworks\esp-idf-v6.0.1`.

For a manual build, use `sdkconfig.defaults`, `devices/sdkconfig.muse`, `devices/sdkconfig.muse-dfrobot-unihiker-k10`, and `devices/sdkconfig.muse-k10-public` in that order, targeting `esp32s3`. See [ESP32 build instructions](esp32/README.md) and [the K10 board guide](esp32/devices/dfrobot-unihiker-k10.md).

## Code organization

[Feature map](esp32/components/muse/FEATURES.md) · [Sensors and rules](esp32/components/muse/sensors/README.md) · [Camera](esp32/components/muse/camera/README.md) · [Streaming speech](esp32/components/muse/tts/README.md) · [USB mode](esp32/components/muse/usb/README.md) · [Credential storage](esp32/main/credentials/README.md) · [USB protocol and page](esp32/tools/muse/usb-config/README.md)

The upstream SDK's other board and Linux sources are retained. K10 extensions are selected by their board/feature options.

## Known limitations

Camera images can be overexposed or color-shifted, especially near bright windows. Ambient light is an estimated lux value, acceleration is signed raw sensor data, and board heat can affect temperature readings. Network interruptions can pause streamed audio; service/key/quota errors leave text replies available. NVS is currently unencrypted; do not distribute a dump of a configured device.

## License and SDK terms

See [LICENSE](LICENSE) for the Apache-2.0 license and retain bundled third-party notices. Muse SDK use is also subject to [Muse Gadgets SDK terms](https://gadgets.muse.ai/sdk-terms). Supplying individual tokens does not waive those terms. This repository contains no shared service credentials.
