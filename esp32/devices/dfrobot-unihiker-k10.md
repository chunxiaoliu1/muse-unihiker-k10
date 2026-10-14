# DFRobot UNIHIKER K10

Experimental ESP-IDF 6.0.1 port for the K10 N16R8 (16 MB flash, 8 MB octal
PSRAM). First version: Muse phone pairing, Wi-Fi, Chinese reply captions,
A-button push-to-talk, B-button menu, and three RGB status LEDs.

Hardware references: [official hardware manual](https://www.unihiker.com.cn/wiki/k10/jianjie),
[schematic V1.2](https://img.dfrobot.com.cn/wikicn/5cabf4771804207b131ae8cb/36b61e6635492b977df87d55fe5dca4e.pdf),
and [DFRobot Arduino sources](https://github.com/DFRobot/framework-arduinounihiker).

| Device | Wiring |
|---|---|
| ILI9341, 240x320 SPI LCD | MOSI 21, SCLK 12, CS 14, DC 13; no touch |
| XL9535 I/O expander | I2C SDA 47 / SCL 48, address 0x20 |
| Backlight / camera reset | Expander bits 0 / 1 |
| A / B button | Expander bits 12 / 2, active low |
| ES7243E microphone ADC | MCLK 3, BCLK 0, WS 38, DIN 39; address probed |
| NS4168 speaker | Shared I2S clocks, DOUT 45 |
| WS2812 RGB LEDs | GPIO46, 3 LEDs, GRB |
| GC2145 camera | D0..D7 8/10/11/9/18/16/15/6, XCLK7, VSYNC4, HREF5, PCLK17; shared SDA47/SCL48 |

GPIO0 is the I2S clock, so **BOOT is not a talk button**. Use A. A also
confirms the phone pairing prompt. B opens and navigates the two-button menu.
Screen off can be woken with A/B; menu Power Off enters deep sleep and needs
RESET to restart. Backlight control is on/off; brightness percentages do not
provide PWM dimming. No battery gauge is exposed.

Implementation and extension guide: [`components/muse/sensors/README.md`](../components/muse/sensors/README.md).

Onboard sensors share SDA47/SCL48: AHT20 temperature/humidity at 0x38,
LTR303 ambient light at 0x29, SC7A20H acceleration at 0x19. Press B to open
the menu, B again to select Sensors, then A to open it. A or B returns to
the list. The page refreshes every 250 ms; temperature/humidity is sampled
about every 2 seconds with CRC checking. Light is an estimated lux value
from the vendor coefficients (1x gain, 100 ms integration); acceleration
is signed raw 12-bit X/Y/Z, matching the vendor API, not a calibrated g
measurement. Missing/failed sensors display Unavailable.
Serial command `>sensors` prints a JSON snapshot; periodic `@sensors` lines
are printed about every 2 seconds. `python tools/muse/sensors.py COM10 10`
reads them for 10 seconds without restarting the device. Flags indicate validity; numeric fields
must be ignored when their corresponding flag is false. Tilt the board to
test acceleration and cover the light sensor to test its response.
Muse can query the same cached readings through the native `sensors.read`
command, advertised by `link.register` and dispatched through `link.invoke`.
Results contain temperature, humidity, light and acceleration, with value,
unit and age_s. Light is marked estimated; acceleration uses raw_12bit units.
Missing/failed samples or samples older than 10 seconds are null. With no
valid readings the command returns no_readings. Sampling stays in the sensor
task, so command handling does not perform I2C transactions. No periodic data
is sent to Muse: it queries on demand. `>sensors.muse` prints the exact command
result on serial for diagnostics. After flashing, let Link reconnect to
advertise the command, then ask Muse to read this K10's temperature, humidity,
light or acceleration. Onboard temperature can be affected by board heat.

Humidity alerts are automatic: a valid reading above 70%RH sends a device
message saying the environment humidity is too high, including the measured
RH. While RH stays above 70%, another reminder is due 15 minutes after the
previous successful acknowledgement. RH at or below 65% rearms the initial
alert; values in the 65-70% band suppress sends without resetting the timer.
Only samples no older than 10 seconds are used. Offline readings are not
replayed as historical alarms: after reconnecting, the current RH must still
be above 70%. Failed sends retry after 60 seconds. State is kept in RAM; a
restart rearms the alert. Messages go to the Muse main chat through the
official device-originated POST /chat/stream route with device_id attribution.
They do not take over the K10 voice turn or play a local alarm.

Serial `>humidity.alert.selftest` checks threshold boundaries, the 15-minute
deadline, hysteresis, invalid samples and retry using a simulated clock.
`>humidity.alert.test` queues a clearly labeled simulated 75%RH message; it
does not change real readings or the production reminder timer. It should
produce a `humidity alert TEST acknowledged` log with a server message_id.

Replies default to text. Optional speech connects directly to Cartesia over TLS
WebSocket and plays streamed 16 kHz PCM with a bounded receive buffer and
one-second prebuffer. Users configure their own API key, voice ID and language
with the USB HTML tool, then restart. No computer service is required. TTS
failures preserve captions. See `components/muse/tts/README.md`.
TF card and original K10 offline AI models are not integrated. Muse uses its
own flash partition map.

On-demand camera support (`CONFIG_MUSE_K10_CAMERA=y`, default on K10)
advertises `camera.capture`. Muse can request a fresh 640x480 JPEG from the
onboard GC2145 after understanding the user's request. Keep the camera aimed
until the screen says the photo was taken. The command returns jpeg-base64
with MIME type, dimensions and byte count. An on-device Muse test on 2026-10-13
confirmed the command invocation and an image-based reply describing the ceiling
and window in the captured scene. Bright-window images were overexposed with a
green tint; image quality still needs tuning. Voice uploads do not attach a photo
automatically.
Capture runs in a worker, then stops the camera and releases the RGB/DMA buffers.
There is no continuous video upload or local face-recognition model.
See [`camera/README.md`](../components/muse/camera/README.md) for wiring,
resource limits and diagnostics. `python tools/muse/camera.py COM10 photo.jpg`
saves a diagnostic image locally, using the same capture code.

The added features are grouped into `components/muse/sensors/`, `camera/`
and `tts/`; the computer-side speech service lives in `tools/muse/tts/`.
See the [feature map](../components/muse/FEATURES.md) for module boundaries
and where to change parameters, event rules, drivers and speech providers.

On Windows, from an activated ESP-IDF PowerShell:

```powershell
./tools/muse/k10.ps1 build
./tools/muse/k10.ps1 flash COM10
./tools/muse/k10.ps1 monitor COM10
```

Set the SDK token in the ignored `build-muse-dfrobot-unihiker-k10/sdkconfig`,
never in the board overlay. Pair using the phone Muse app's Settings >
Devices > Developer mode > Add Device. Choose `MuseGadget-XXXXXX`.

For user-distributed firmware, run `tools/muse/k10-public.ps1 -Action package`.
This uses an isolated runtime-only build with no embedded SDK Token, Wi-Fi
credentials or personal TTS URL. Users enter their own SDK Token through the
[USB configuration page](../tools/muse/usb-config/README.md), restart and pair.
SDK credentials live in [main/credentials](../main/credentials/README.md).

RGB: breathing orange = setup, breathing blue = pairing/connecting,
green = online, blue = recording, breathing purple = thinking,
breathing yellow = reconnecting, breathing red = error.
