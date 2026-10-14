# Muse for UNIHIKER K10

**Talk to Muse, see and hear its replies, ask about your surroundings, and let K10 report sensor events.**

This firmware turns [DFRobot UNIHIKER K10](https://www.dfrobot.com/product-2904.html) into a Muse-connected device using its screen, microphone, speaker, camera, sensors and RGB LEDs. It is a community extension of [Meta's Muse Gadgets SDK](https://github.com/facebookincubator/muse-gadget-sdk).

## Features at a glance

| Feature | What you can do | What you need |
|---|---|---|
| Voice conversation | Hold A to speak, release to send, and read Muse's reply on K10 | Muse pairing and internet access |
| Animated screen | See an animated avatar, reply text and connection status; multilingual text, including Chinese, is supported | Included |
| Spoken replies | Have K10 read replies aloud using streamed audio from Cartesia | Optional personal Cartesia API Key |
| Sensor readings | Check temperature, humidity, ambient light and three-axis acceleration on K10 or ask Muse for them | Included; Muse queries need a connection |
| Humidity reminders | Send a message to Muse when humidity exceeds 70% RH; repeat every 15 minutes while it stays high | K10 online and connected to Muse |
| Camera questions | Point K10 at an object and ask Muse what it is; Muse requests a photo when needed | Muse pairing and internet access |
| USB setup | Set your own SDK Token, Cartesia key, voice and language from a single HTML file | USB data cable and desktop Chrome/Edge |
| Device controls | Use the A/B button menu for sensors, volume, speaker, microphone gain, connection settings and USB mode | Included |
| RGB status lights | See whether K10 is setting up, connecting, online, recording, thinking or taking a photo | Included |

Voice replies are optional. Without a Cartesia key, voice input and on-screen Muse replies still work.

## How the features work

### Talk to Muse

Hold **A** while speaking, then release it. K10 sends your recording to Muse and displays the reply on its screen. For example:

> Tell me a short story.
>
> Help me plan my day.

The interface and setup guides are in English. Muse replies can still use other languages, and CJK text rendering is retained.

### Hear replies aloud

Enter your own [Cartesia API Key](https://play.cartesia.ai/keys), choose a Voice ID and reply language, and save the settings. K10 receives and plays a continuous PCM audio stream directly from Cartesia. No computer-side speech service is required.

The setup page defaults to English for new settings; existing device language settings are preserved. Use **Test voice** after saving, restarting and reconnecting to check playback. Voice synthesis sends reply text to Cartesia and consumes your service credits. Network or service failures leave the text reply available.

### Read the onboard sensors

Hold **A** and ask Muse about the onboard sensors, then release **A** to send your question. For example:

> Read the temperature and humidity from my K10.
>
> How bright is it around my K10?
>
> Read my K10's acceleration values.

| Sensor | Available readings | Notes |
|---|---|---|
| AHT20 | Temperature in degrees Celsius; relative humidity in % RH | Temperature can be affected by board heat |
| LTR303 | Ambient light in estimated lux | Useful for checking changes in lighting |
| SC7A20H | Three-axis X/Y/Z acceleration | Signed raw 12-bit values, not calibrated g |

The screen shows locally sampled readings. Muse requests the same cached data through `sensors.read`; readings include units and sample age. Missing, invalid or older-than-10-second readings are not reported as valid measurements. Ordinary sensor readings are queried on demand rather than continuously sent to Muse.

### Receive high-humidity reminders

K10 monitors humidity automatically:

- **Above 70% RH:** send a message to Muse stating that ambient humidity is too high and include the measured value.
- **Still above 70% RH:** remind again 15 minutes after the last successfully delivered reminder.
- **At or below 65% RH:** rearm, so the next rise above 70% can trigger a new initial reminder.

These are messages in Muse's chat, not a local audible alarm. After reconnecting, K10 checks the current reading rather than replaying old offline alarms. Thresholds, repeat intervals and additional sensor rules can be changed in the [sensor module](esp32/components/muse/sensors/README.md).

### Ask Muse to look at an object

Point the camera at an object, hold **A**, and say:

> Look at this object and tell me what it is.

The flow is:

1. K10 sends your voice request to Muse.
2. Muse decides whether to request an image and invokes `camera.capture` when needed.
3. K10 takes a photo and returns a JPEG.
4. Muse analyzes the image and sends its answer back to K10.

Keep the camera pointed at the object until capture finishes. Photos are taken on demand; there is no continuous video stream. The current camera output is 640 x 480. Bright windows can cause overexposure or color casts, so use normal indoor lighting where possible.

### Configure everything over USB

Open the standalone **`k10-usb-config.html`** in desktop Chrome or Edge. It lets you:

- Set or replace your Muse Gadgets SDK Token.
- Set your optional Cartesia key, voice and language.
- Save all entered settings with one button and restart automatically after every save is confirmed.
- Test saved voice settings and refresh device status.
- Clear only the SDK Token, disable voice replies, or clear all user settings before switching accounts or transferring the device.

Blank credential fields preserve existing settings. To change a stored voice or language, enter the Cartesia key again. If a later save fails, the page identifies confirmed saves and does not request a restart.

For first-time setup, K10 keeps its animated avatar and displays:

```text
USB
Set SDK Token first.
```

USB configuration stays available until a token is saved and K10 restarts. On an already configured device, select **USB mode** in the menu before connecting. Manual USB mode locks the configuration page until restart. Physical buttons are ignored during USB configuration; Wi-Fi, Muse and speech services continue in the background.

Normal startup with a configured token creates no dedicated USB configuration task, request queue or driver buffers.

### Buttons and status lights

| Control | Action |
|---|---|
| Hold A, then release | Record and send a voice request during normal operation |
| A while a pairing prompt is shown | Confirm pairing |
| B | Open the menu or move to the next item |
| A in the menu | Select the highlighted item |
| RESET | Restart K10; leave USB mode when a token is configured |

| RGB indication | Meaning |
|---|---|
| Breathing amber | Setup, offline or reconnecting |
| Breathing blue | Pairing or connecting |
| Green | Online and ready |
| Blue | Recording |
| Breathing purple | Thinking, or capturing a photo while online |
| Breathing red | Error |

## What you need

- **[DFRobot UNIHIKER K10](https://www.dfrobot.com/product-2904.html)**: ESP32-S3, 16 MB flash, 8 MB PSRAM. Available from the [official DFRobot store](https://www.dfrobot.com/product-2904.html) or [global distributors](https://www.unihiker.com/distributors).
- A USB data cable (**included with K10**) and a computer running **Chrome or Edge**.
- The Muse phone app with **Developer mode** available.
- Your own **Muse Gadgets SDK Token**, from Account -> SDK tokens at [gadgets.muse.ai](https://gadgets.muse.ai).
- Optional: a personal **Cartesia API Key** for spoken replies.

Muse chat, image analysis and Cartesia speech require internet access. This firmware does not integrate the original K10 offline AI models, TF-card features, local image recognition or a wake-word assistant.

## Quick start

1. Download the public firmware package from [Releases](https://github.com/chunxiaoliu1/muse-unihiker-k10/releases).
2. Follow the [flash and setup guide](esp32/tools/muse/K10_PUBLIC_RELEASE.md). The package contains four images with specific flash addresses; do not flash the application BIN alone at address 0x0.
3. Boot K10 normally and open the included `usb-config/k10-usb-config.html`.
4. Connect K10, enter your SDK Token and optional voice settings, then choose **Save all settings**.
5. After K10 restarts, enable Developer mode in the Muse app, add the device and set up Wi-Fi.
6. Hold **A**, speak, release, and read the reply. If Cartesia is configured, K10 also reads it aloud.

After clearing all local user settings, remove the old K10 entry in the Muse app before pairing again. Reflashing alone preserves saved settings.

## Build from source

Use **ESP-IDF v6.0.1**. From `esp32/`, with IDF activated:

```powershell
./tools/muse/k10-public.ps1 -Action build
```

The public profile accepts credentials at runtime and needs no developer token at build time. The audited firmware package and configuration page are written to `esp32/build-muse-k10-public/release/`.

Windows helpers use the configured IDF environment, or the installation at `C:\Espressif\frameworks\esp-idf-v6.0.1`. For a manual build, target `esp32s3` and load `sdkconfig.defaults`, `devices/sdkconfig.muse`, `devices/sdkconfig.muse-dfrobot-unihiker-k10` and `devices/sdkconfig.muse-k10-public` in that order.

See [ESP32 build instructions](esp32/README.md) and [the K10 board guide](esp32/devices/dfrobot-unihiker-k10.md).

## Extend the project

| Area | Guide |
|---|---|
| Module boundaries and entry points | [Feature map](esp32/components/muse/FEATURES.md) |
| Add sensors or adjust event rules | [Sensors and rules](esp32/components/muse/sensors/README.md) |
| Capture parameters and camera driver | [Camera](esp32/components/muse/camera/README.md) |
| Streaming speech and buffering | [Speech synthesis](esp32/components/muse/tts/README.md) |
| USB resource lifecycle | [USB mode](esp32/components/muse/usb/README.md) |
| NVS and credential handling | [Credential storage](esp32/main/credentials/README.md) |
| Configuration page and serial protocol | [USB configuration](esp32/tools/muse/usb-config/README.md) |

The upstream SDK's other board and Linux sources are retained. K10 extensions are selected by board and feature options. Original upstream documentation is preserved in [README_UPSTREAM.md](README_UPSTREAM.md).

## Credentials, limitations and licensing

No developer SDK Token, Cartesia key, Wi-Fi password or device NVS is included. The USB page does not upload, store in the browser, or read back credentials. NVS is currently unencrypted; do not distribute a dump of a configured device.

Camera quality, estimated light levels, raw acceleration and streamed-audio pauses are described with their respective features above. This is a community firmware project, not an official DFRobot or Meta release.

See [LICENSE](LICENSE) for Apache-2.0 and retain bundled third-party notices. Muse SDK use is also subject to [Muse Gadgets SDK terms](https://gadgets.muse.ai/sdk-terms); using individual tokens does not waive those terms.
