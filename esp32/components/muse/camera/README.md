# K10 camera

This directory owns camera logic and acquisition behind the common `components/camera` interface. `CONFIG_MUSE_K10_CAMERA` controls the backend, serial command, and Home Link `camera.capture` registration and dispatch. It is enabled by default for K10; other boards do not load this driver dependency.

```text
camera/
  k10_camera.h           Public board, Muse and serial interface
  k10_camera.c           Command description, Base64/JSON, busy state, captions, diagnostics
  k10_camera_config.h    Pins, timing, dimensions, JPEG quality/capacity, stacks, timeout
  drivers/gc2145.c       Initialization, warm-up, RGB565 acquisition, JPEG encoding, cleanup/reset
  drivers/gc2145.h       Internal driver registration
```

`main/noise_control.cpp` obtains the command description and timeout from the module. `main/app.c` handles request IDs, session generations, background tasks and result delivery. Shared protocol code contains no K10 pin map or encoding logic. Adjust exposure, warm-up and encoding in `drivers/gc2145.c` and the configuration header. Adjust result fields and captions in `k10_camera.c`. The board retains the reset callback because XL9535 also controls the backlight.

Muse requests images on demand: send speech to Muse, Muse invokes `camera.capture`, K10 takes a photo and returns a JPEG, and Muse answers using that image. This flow has been verified on a physical K10. Sending speech alone neither takes a photo automatically nor attaches an image.

The pin map follows DFRobot `who_camera.h`: D0..D7 = 8/10/11/9/18/16/15/6; XCLK = 7 at 16 MHz; VSYNC = 4; HREF = 5; PCLK = 17. SCCB shares board I2C0 on SDA47/SCL48. XL9535 P01 reset uses the board callback and the backlight output lock; camera code must not independently write the output register.

GC2145 outputs RGB565; ESP32 encodes JPEG in software. `CONFIG_CAMERA_PSRAM_DMA=y` writes directly to PSRAM, avoiding approximately 30 KiB of additional contiguous internal DMA memory. Each request initializes the camera with one PSRAM frame buffer, discards two warm-up frames, and encodes the third. It then returns the frame, frees driver resources, stops XCLK and holds reset. There is no continuous viewfinder or background upload.

The display adapter, RGB, button and serial tasks use PSRAM stacks to leave internal RAM for camera DMA descriptors and the capture task while online. After initialization, the driver also verifies that `cam_task` exists: upstream does not check task-creation failure, so this returns an explicit memory error instead of reporting success and waiting forever for a frame.

`k10_camera_config.h` currently selects VGA 640x480 and JPEG quality 75. Images exceeding 96 KiB are retried at quality 50; if still too large, the request fails instead of returning a truncated image. Change `K10_CAMERA_FRAME_SIZE`, `K10_CAMERA_WIDTH` and `K10_CAMERA_HEIGHT` together. Dimension checks and command descriptions use these settings. Warm-up frame counts, frame delays and reset timing also live here.

Capture runs in a separate task while Link continues serving the network. Concurrent requests return `camera_busy`. Results use `payload.format=jpeg-base64` and `payload.data_base64`, plus `mime_type=image/jpeg`, width, height and bytes. Failures return `ok=false` and an error. Captions ask the user to hold steady; RGB pulses purple while capturing online.

Serial `>camera.capture` uses the same capture function and emits `@camera.capture {JSON}` from its task:

```powershell
python tools/muse/camera.py COM10 build-muse-dfrobot-unihiker-k10/camera-test.jpg
```

The tool saves the image locally without sending it to Muse. Check JPEG detail, orientation and sensor readings after capture. Then point at an object, hold A, and say: "Use this K10's camera and tell me what this is." Confirm `link.invoke camera.capture`, capture logs and a correct Muse answer.

## Hardware integration record, 2026-10-13

A K10 connected over Wi-Fi with Link online received a Muse `camera.capture` request and returned a 640x480 JPEG of 10,731 bytes in 1,408 ms. Muse described the ceiling, upper window edge and bright light, and noted overexposure and a green cast. A subsequent local capture matched the scene.

On-demand capture and image analysis are verified. Image quality still needs work: bright window scenes can be overexposed and green. One earlier capture showed localized color artifacts that did not recur; the cause is unconfirmed. Use normal indoor lighting for object recognition and avoid pointing directly at bright windows.
