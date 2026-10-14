# K10 USB configuration mode

`usb_mode.c/.h` manages USB setup resources for the current boot. The mode is not stored in NVS.

- No correctly formatted SDK Token in the boot cache: automatically enable USB setup indefinitely. Keep the normal startup animation and avatar, with `USB` and `Set SDK Token first.` in the header. Do not open the USB menu page.
- Token configured: start Off, without a serial task, USB driver or SDK request queue.
- Selecting **USB mode** in settings asks the keeper to allocate resources without restarting.
- The manually opened USB page stays locked until restart: no Back controls, and other screens cannot close it. Both USB setup states ignore physical button input.
- Disconnecting the page does not exit USB mode. Wi-Fi, Muse and TTS continue running.
- Restart checks the boot cache again; a configured token restores Off.

Menu/UI code only changes atomic state and notifies the keeper. The keeper creates the credential queue, opens the existing console VFS and creates a PSRAM-stack serial task. Startup failure closes the descriptor and releases all newly created dedicated resources. A failed manual entry can be retried after restart.

Even in setup mode, the USB Serial/JTAG driver is not installed: no RX/TX ring buffers or driver interrupt are allocated. The receive task reads the hardware FIFO and yields for 2 ms when idle; transmission uses the VFS lock shared with logging. ESP-IDF 6.0.1 VFS nonblocking reads query only driver buffers, so they cannot receive without a driver.

The existing internal-stack keeper still handles Wi-Fi/NVS; it is not a USB-only task. Network and TTS remain available, including browser voice tests.

`muse_input.c` parses serial commands; `muse_link` notifies the keeper; `main/credentials` owns the queue, NVS and boot cache. Only the keeper allocates or rolls back the queue, preventing UI deletion races. Off mode has no dedicated polling or waiting task.

The hardware USB/ROM bootloader and log output may still appear on the computer. This is separate from configuration-channel activation. Serial diagnostics also require USB mode unless first-time setup enabled it automatically.
