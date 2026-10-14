# UNIHIKER K10 · Muse public firmware

Requires UNIHIKER K10 (ESP32-S3, 16 MB flash, 8 MB PSRAM). Each user supplies their own Muse Gadgets SDK Token. This package contains no developer SDK Token, Wi-Fi password, device NVS, or personal TTS service credentials.

## Flashing

The same four firmware images work with Windows, macOS and Linux. Install Python and esptool in your Python environment:

```sh
python -m pip install esptool
```

If your system uses `python3`, substitute `python3` for `python` in the commands below. Connect K10 by USB, hold BOOT, press RESET once, then release BOOT. Find its serial port with:

```sh
python -m serial.tools.list_ports
```

Extract the package and open a terminal in its folder. Replace `YOUR_PORT` with the device port, for example `COM14` on Windows, `/dev/cu.usbmodem...` on macOS, or `/dev/ttyACM0` on Linux. Run this as one line:

```sh
python -m esptool --chip esp32s3 --port YOUR_PORT --baud 460800 write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB 0x0 firmware/bootloader.bin 0x10000 firmware/partition-table.bin 0x1d000 firmware/ota_data_initial.bin 0x20000 firmware/muse-gadget.bin
```

The command writes these files without erasing the entire flash or overwriting NVS:

| File | Address |
|---|---|
| firmware/bootloader.bin | 0x0 |
| firmware/partition-table.bin | 0x10000 |
| firmware/ota_data_initial.bin | 0x1D000 |
| firmware/muse-gadget.bin | 0x20000 |

Alternatively, use ESP Flash Download Tool with ESP32-S3, DIO, 80 MHz, 16 MB and the four addresses above. `muse-gadget.bin` is the application image; do not flash it alone at 0x0. This is a full USB installation package. Back up any data needed from vendor firmware with a different partition table. After flashing, release BOOT and press RESET once to boot normally.

## Setup and pairing

Open `usb-config/k10-usb-config.html` in desktop Chrome or Edge. This standalone file includes its scripts and styles. If your browser blocks serial access from local files, serve it over HTTPS or localhost. No backend API is needed. Only this standalone HTML is needed for setup. Its editable source files remain in the GitHub repository and are not duplicated in the download package.

For local use, run `python -m http.server 8766 --bind 127.0.0.1` in `usb-config/` and open `http://localhost:8766`.

Connect the device, enter your SDK Token and optional voice settings, then choose **Save all settings** at the bottom. K10 restarts only after every entered setting is confirmed saved. Blank Token/Key fields preserve existing settings; changing a saved voice or language requires entering the key again.

Without a valid-format SDK Token, the avatar remains visible with `USB` and `Set SDK Token first.` in the header, and USB setup stays available. With a configured token, normal startup allocates no USB configuration task, driver buffers or request queue. To configure it again, select **USB mode** in K10's menu. This does not restart K10; the configuration page stays locked until restart. Physical buttons are ignored during USB setup while Wi-Fi, Muse and TTS continue in the background. Disconnecting the page does not leave the mode. Restart with a configured token returns to normal operation.

Local validation checks token presence and format; Muse verifies cloud validity during pairing. Enable Developer mode in the phone's Muse app, add K10, select Wi-Fi and complete pairing. After clearing local settings, remove the old device in the app before adding it again. Get an SDK Token from https://gadgets.muse.ai under Account -> SDK tokens. Pairing supplies the separate device and refresh tokens.

Before switching accounts or transferring a device, choose **Clear all user settings**. Reflashing alone preserves pairing. Full clearing removes the SDK Token, Cartesia key/voice/language, account pairing, Wi-Fi, and manually configured Muse Token/VM. Pairing rollback, ordinary unpairing and the menu's pairing reset preserve the SDK Token. Local clearing does not revoke a token on the website; volume and brightness preferences are preserved.

## Features

- Multilingual text replies, including Chinese.
- Hold A for voice input; RGB LEDs indicate status.
- Temperature, humidity, light and acceleration readings.
- High-humidity reminders: above 70% RH, repeat every 15 minutes while high, rearm at or below 65% RH.
- Camera photos requested by Muse on demand. Images may still be overexposed or color-shifted; this release does not claim to resolve image quality.
- Optional Cartesia PCM streaming voice replies. No computer-side TTS service is required.

Voice is off until a user enters their own Cartesia API Key, Voice ID and language. Use **Save all settings** to apply them and restart. New settings default to English; existing settings are preserved. **Test voice** uses your Cartesia credits; refresh status to check the result. TTS updates preserve Muse pairing and Wi-Fi. Invalid keys, insufficient credits or network failure leave text replies available. Keys are neither embedded in firmware nor returned to the page. Enabling TTS sends reply text to Cartesia.

The package includes four BIN files, one USB configuration HTML, this guide, checksums and license notices. It contains no operating-system-specific flash script. See `licenses/README.md` for the license directory. Open-source licenses and third-party notices are included. SDK terms: https://gadgets.muse.ai/sdk-terms. Check the terms and resource licenses for your use; supplying individual tokens does not waive SDK terms. Verify file integrity with `SHA256SUMS.txt`.
