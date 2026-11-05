# ESP32 bring-up (C3 and S3) — measurements and decisions

> **Status:** the port is implemented in [`firmware/`](../firmware/) with the
> envs `esp32c3`, `esp32s3`, `esp32cam` and `esp32dev`. This document records
> the bring-up and the hardware measurements that supported the target
> selection. For build, configuration, and limitations of the final firmware,
> see [`firmware/README.md`](../firmware/README.md). The comparison of the three
> ESP32-CAM boards is in [`esp32cam-boards.md`](esp32cam-boards.md).

Record of the work done to validate the feasibility of the X5 bridge port. Use
it to avoid repeating the investigation and to keep the configurations that
worked.

## Tools used

| Tool | Version | Use |
|---|---|---|
| PlatformIO Core | 6.2.0 (`~/.platformio/penv/bin/pio`) | build, upload, monitor |
| platform espressif32 | 7.1.3 (6.12.0 also installed) | toolchain/framework |
| framework-arduinoespressif32 | 4.20017.260907 (reports IDF 4.4.7) | probe firmware |
| toolchain-riscv32-esp | 8.4.0+2021r2-patch5 | C3 |
| toolchain-xtensa-esp32s3 | 8.4.0+2021r2-patch5 | S3 |
| esptool (system) | 4.7.0 (`/usr/bin/esptool`) | `chip_id` |
| esptool (PlatformIO) | 4.11.0 (`~/.platformio/packages/tool-esptoolpy/esptool.py`) | fallback for S3 |
| pyserial (penv) | — | USB-CDC serial read |

> The system esptool 4.7.0 **cannot flash/read the S3** (missing
> `stub_flasher_32s3.json`). Use the PlatformIO esptool or `pio run`. For the
> C3, the system esptool works.

## Probe firmware

Source: [`tools/esp-probe/`](../tools/esp-probe/) (`platformio.ini` +
`src/main.cpp`). It prints chip, revision, cores, frequency, flash, PSRAM, heap,
and WiFi scan. It also tests allocations of 16/20/24/32 KB (3 of each).

### Commands

```bash
# C3
~/.platformio/penv/bin/pio run -d tools/esp-probe -e esp32-c3 -t upload

# S3 (module with 4MB flash + 2MB PSRAM)
~/.platformio/penv/bin/pio run -d tools/esp-probe -e esp32-s3 -t upload

# ESP32-CAM (AI-Thinker, CH340 on /dev/ttyUSB0; includes the OV2640 test)
~/.platformio/penv/bin/pio run -d tools/esp-probe -e esp32cam -t upload

# serial read (native USB-CDC)
~/.platformio/penv/bin/python - <<'PY'
import serial, glob, time
p = serial.Serial(sorted(glob.glob('/dev/ttyACM*'))[0], 115200, timeout=1)
t = time.time()
while time.time() - t < 20:
    c = p.read(4096)
    if c: print(c.decode(errors='replace'), end='')
PY
```

## Result — ESP32-C3

| Item | Value |
|---|---|
| Chip | ESP32-C3 (QFN32), **rev v0.4** |
| Cores / clock | 1 / 160 MHz |
| Flash | 4 MB embedded (XMC), 80 MHz |
| PSRAM | **none** |
| Radio | WiFi 4 + BLE |
| USB | native Espressif (VID:PID `303a:1001`), JTAG/serial |
| MAC | `1C:DB:D4:E3:8A:D4` |
| SDK (Arduino) | `v4.4.7-dirty` |
| Heap total / free | 297,988 B / **234,052 B** |
| Heap minimum / largest block | 135,700 B / 212,980 B |
| Allocations | 3×16 KB, 3×20 KB, 3×24 KB, 3×32 KB — **all OK** |

WiFi scan: 8–9 networks; channel 1 strongest/busiest (`ELEUSIS` −51,
`#CLARO-WIFI` −50).

**Conclusion:** 2×24 KB of frame pool + lwIP + HTTP fit with margin. The C3 is
**sufficient** for 1 camera and 1–2 MJPEG clients.

## Result — ESP32-S3

| Item | Value |
|---|---|
| Chip | ESP32-S3 (QFN56), rev v0.2 (esptool) / v0.0 in Arduino |
| Cores / clock | 2 / 240 MHz |
| Flash | 4 MB embedded (XMC), 80 MHz |
| PSRAM | **2 MB embedded (AP_3v3)**, detected |
| Radio | WiFi 4 + BLE |
| USB | native Espressif (`303a:1001`) |
| MAC | `E4:B3:23:F8:FB:2C` |
| SDK (Arduino) | `v4.4.7-dirty` |
| Internal heap total / free | 360,932 B / **300,828 B** |
| Internal heap minimum / largest block | 202,476 B / 286,708 B |
| PSRAM free / largest block | 2,080,767 B / 2,064,372 B |
| Allocations (internal RAM) | 3×16 KB, 3×20 KB, 3×24 KB, 3×32 KB — **all OK** |

WiFi scan: 3 networks (`ELEUSIS` −62, `#CLARO-WIFI` −62, `DTU:Inverter` −88).
The difference from the C3 is probably antenna/position, not a throughput
measurement.

**Conclusion:** the S3 gives real margin: a 3×24 KB pool in PSRAM, double
buffering, and 2–4 clients, and it can also separate the CAM task (core 0) from
HTTP (core 1).

### Required S3 configuration in PlatformIO

The `esp32-s3-devkitc-1` board assumes 8 MB of flash. For the 4 MB module:

```ini
board_upload.flash_size = 4MB
board_build.partitions = default.csv   ; 4MB table (app 1.2MB)
board_build.arduino.memory_type = qio_qspi  ; 2MB quad PSRAM
build_flags = -DBOARD_HAS_PSRAM
```

Without these settings, the PSRAM does not start (`psram_found = 0`) or the
default_8MB partition is invalid on 4MB flash.

## Result — ESP32-CAM (AI-Thinker)

Board with CH340 on `/dev/ttyUSB0` (no native USB). Probe firmware with
`lib_deps = espressif/esp32-camera` (2.0.4) and AI-Thinker pinout.

| Item | Value |
|---|---|
| Chip | ESP32-D0WD-V3, **rev v0.3** |
| Cores / clock | 2 / 240 MHz |
| Flash | 4 MB (XMC), **40 MHz, DIO mode** |
| PSRAM | **2 MB** (not 4 MB), detected |
| Radio | WiFi 4 + BT/BLE |
| USB | **external CH340** (no native USB/JTAG) |
| MAC | `3C:E9:0E:86:3F:84` |
| SDK (Arduino) | `v4.4.7-dirty` |
| Heap total / free (after WiFi) | 319,036 B / **246,384 B** |
| Largest internal block | **110,580 B** (smaller than C3/S3) |
| PSRAM free / largest block | 2,095,103 B / 2,064,372 B |
| Allocations | 3×16/20/24/32 KB in internal RAM — **OK** |

### OV2640 camera

`camera_init = ESP_OK`, sensor **PID 0x26 (OV2640)**, JPEG. Result for each
resolution:

| Resolution | Result |
|---|---|
| QVGA 320x240 | **OK** (3/3 frames, ~4.5 KB) |
| VGA 640x480 | **OK** (2/2 valid frames after the 1st, ~11 KB) |
| SVGA 800x600 | 1st frame OK, **2nd FAIL** |
| UXGA 1600x1200 | **FAIL** |

Interpretation: the camera is **not completely broken**. It works at QVGA/VGA.
The failure at SVGA/UXGA indicates a limit/damage in the sensor, in the ribbon,
or in the PSRAM/DMA bandwidth. The bridge X5 **does not use** the onboard
camera, so the board is usable: the ESP32, 4 MB of flash, 2 MB of PSRAM, and
WiFi work.

### Observations specific to the ESP32-CAM

- After upload, the serial output can be garbage (`xxxx...`) because the board
  stays in the bootloader/reset state. To read correctly, open the port and
  pulse the reset:
  ```python
  p.setDTR(False); p.setRTS(True); time.sleep(0.15)
  p.setRTS(False); time.sleep(0.2)
  ```
- Upload on `/dev/ttyUSB0` worked directly (the board/adapter has auto-reset).
- With no camera in use, the 2 MB PSRAM is free for the frame pool/buffers.
- The largest **internal** block is only ~110 KB; keep the frame pool in PSRAM.

## Result — old ESP32 DevKit (D0WD-V3)

Generic board with CP2102 on `/dev/ttyUSB0` (no native USB), upload at 115200.

| Item | Value |
|---|---|
| Chip | ESP32-D0WD-V3, **rev v3.1** (esptool) / v0.3 in Arduino |
| Cores / clock | 2 / 240 MHz |
| Flash | 4 MB (XMC), 40 MHz, DIO mode |
| PSRAM | **none** |
| Radio | WiFi 4 + BT/BLE |
| USB | external CP2102 (no native JTAG) |
| MAC | `20:50:0D:E7:16:68` |
| SDK (Arduino) | `v4.4.7-dirty` |
| Heap total / free (after WiFi) | 333,496 B / **261,280 B** |
| Largest internal block | **110,580 B** |
| Allocations | 3×16/20/24/32 KB in internal RAM — **OK** |

WiFi scan: 8 networks, good signal (`ELEUSIS`/`#CLARO-WIFI` −51 dBm).

Observations:
- `upload_speed` had to be **115200**; at 460800 esptool did not connect.
- When you open the serial port with a DTR/RTS pulse, several boots can appear
  in sequence; after that it stabilizes and prints the reports normally.
- No PSRAM: the frame pool (2×24 KB) fits in internal RAM, but it shares space
  with lwIP/TCP. It serves the basic case (1 camera, 1–2 clients).

## Comparison of the tested modules

| Module | CPU | Free RAM after WiFi | Largest internal block | PSRAM | USB |
|---|---|---|---|---|---|
| ESP32-C3 | 1×160 MHz | 234 KB | 213 KB | — | native/JTAG |
| **ESP32-S3** | **2×240 MHz** | **301 KB** | **287 KB** | **2 MB** | **native/JTAG** |
| ESP32-CAM | 2×240 MHz | 246 KB | 110 KB | 2 MB | CH340 |
| ESP32 DevKit | 2×240 MHz | 261 KB | 110 KB | — | CH340 |

## Recommendation

**Most appropriate: ESP32-S3.** It is the only one with simultaneous margin of
CPU (dual-core), internal RAM (301 KB free / 287 KB block), and PSRAM (2 MB),
and it also has native USB/JTAG. It permits the ideal design of the plan: CAM
task on core 0, HTTP on core 1, frame pool of 3×24 KB in PSRAM, and 2–4 clients
with no risk that the HTTP send blocks the UDP receive.

Alternatives, in order:

1. **ESP32-CAM (reused, without the camera)** — best cost/benefit if the idea is
   to use what already exists: dual-core + 2 MB of PSRAM + 246 KB of heap. It is
   below the S3 in internal RAM (110 KB block) and has no native USB.
2. **ESP32-C3** — sufficient for the basic case (1 camera, 1–2 clients), lower
   power, and simpler, but single-core and no PSRAM; less margin for
   multi-client/audio/PTZ.
3. **Old ESP32 DevKit** — it works, but without PSRAM and with a 110 KB internal
   block; use it as a test bench, not as the final target.
4. **ESP32-C6** — not tested; it needs ESP-IDF and, because it is single-core
   without PSRAM, it does not exceed the S3 for this workload.

## Toolchain: what works for each chip

| Target | Arduino (PlatformIO 7.1.3) | ESP-IDF (PlatformIO) |
|---|---|---|
| ESP32-C3 | **yes** (tested) | yes |
| ESP32-S3 | **yes** (tested) | yes |
| ESP32 (D0WD-V3) | **yes** (tested on the ESP32-CAM) | yes |
| ESP32-C6 | **no** (the board declares only `espidf`) | yes |

Consequence for the plan: if the target is the **C6**, the project must start in
**ESP-IDF** (not Arduino with this PlatformIO installation). For the **C3/S3**,
Arduino works, but the installed core uses **IDF 4.4.7**.

## Updates to the port plan

1. **The C3 becomes the main development target** (hardware already available
   and measured).
2. C3: frame pool of **2 slots × 24 KB**, `max_open_sockets = 2`, non-blocking
   HTTP send, priority CAM task (single-core).
3. S3: frame pool of **3 slots × 24 KB in PSRAM**, CAM task on core 0 and HTTP
   on core 1, up to 4 clients. Hot buffers (UDP RX, lwIP) stay in internal RAM.
4. Do not port `std::thread`/`std::vector`/blocking `send()` from `serve.cpp`.
5. `WiFi.setSleep(false)` (`WIFI_PS_NONE`) on both.
6. Native USB-CDC removes the need for an external UART; the C3/S3 have
   embedded JTAG. The ESP32-CAM uses a CH340 and needs the correct DTR/RTS reset
   for the console.
7. If the target is the C6: plan the ESP-IDF installation and rewrite the
   component plan for IDF (do not depend on the Arduino core).
8. **The ESP32-CAM is reusable as a bridge** (onboard camera out of scope):
   dual-core, 4 MB flash, 2 MB PSRAM. The frame pool must go to PSRAM, because
   the largest internal block is only ~110 KB. It has no native USB/JTAG.
9. The OV2640 camera of the ESP32-CAM works at QVGA/VGA and fails at SVGA/UXGA.
   If you use it one day, limit it to VGA and investigate the ribbon/power
   supply.

## Observations

- The C3 flash was essentially **empty** before the probe (100% `0xFF`, except
  for 24 bytes at `0x50000`), so there was no firmware to preserve.
- The probe stays in the board; to flash another firmware, run
  `pio run -t upload`.
- The Arduino report shows `chip_revision v0.0` on the S3, which differs from
  `esptool` (`v0.2`). Use the esptool value as the reference.
- The heap measurements were done **without association to the AP**. After a
  real WiFi connection, expect an additional drop (queue/association) and
  revalidate with the camera.

## Final status

The final firmware is in [`firmware/`](../firmware/) and has four envs:
**ESP32-C3** (`esp32c3`), **ESP32-S3** (`esp32s3`), **ESP32-CAM** (`esp32cam`),
and **ESP32 DevKit** (`esp32dev`). It was validated with one X5 camera on all of
them: discovery, MJPEG, snapshot, MQTT, OTA, `/config`, and `/reboot`. On the
S3, the 2 MB PSRAM and the dual-core were active, but the RSSI in the test was
~-64/-65 dBm against ~-50 dBm for the C3; the FPS was limited by WiFi. The three
ESP32-CAM boards were compared in
[`esp32cam-boards.md`](esp32cam-boards.md); the board with the broken camera was
used as the bridge (the onboard camera is not used). In the two-camera test, the
ESP32-CAM worked but with low FPS; on the S3, CAM1 (`.71`) worked and CAM0
(`.10`) stayed ONLINE with a ticket and 0 frames. Measurements, limits, and open
items are in [`firmware/README.md`](../firmware/README.md).
