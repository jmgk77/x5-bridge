# ESP32-C3 / S3 / ESP32-CAM / ESP32 DevKit firmware — X5 Bridge

Bridge firmware for X5 cameras, with targets **ESP32-C3**, **ESP32-S3**,
**ESP32-CAM** and **ESP32 DevKit**. It uses the same stack as the other devices
in the house: `ESPAsyncWiFiManager`, `ESPAsyncWebServer`,
`ESPAsyncHTTPUpdateServer` and `PicoMQTT`. The video uses raw TCP on ports
separate from the administration server.

## Hardware

- ESP32-C3 (tested: QFN32 rev v0.4, 4 MB flash, no PSRAM, single-core).
- ESP32-S3 (tested: 4 MB flash, 2 MB PSRAM, dual-core 240 MHz).
- ESP32-CAM (AI-Thinker, 4 MB flash, 2 MB PSRAM, CH340; the bridge does not use
  the onboard camera).
- ESP32 DevKit (D0WD-V3, 4 MB flash, no PSRAM, CP2102).
- Native USB/JTAG (C3/S3) or USB-UART CH340/CP2102 (CAM/DevKit).
- WiFi 2.4 GHz.

## Project structure

```
firmware/
  platformio.ini
  buildscript_versioning.py     # generates include/version.h and increments versioning
  copy_firmware.py              # copies the .bin to the project root
  versioning                    # build counter
  include/
    main.h  config.h  version.h (generated)
    eeprom.h  web.h  mqtt.h  camera.h  stream.h  dump.h  logger.h
  src/
    main.cpp  eeprom.cpp  web.cpp  mqtt.cpp
    camera.cpp  stream.cpp  dump.cpp  logger.cpp
```

## Build and flash

```bash
# ESP32-C3
~/.platformio/penv/bin/pio run -d firmware -e esp32c3
~/.platformio/penv/bin/pio run -d firmware -e esp32c3 -t upload
~/.platformio/penv/bin/pio run -d firmware -e esp32c3 -t monitor

# ESP32-S3 (4 MB flash + 2 MB PSRAM)
~/.platformio/penv/bin/pio run -d firmware -e esp32s3
~/.platformio/penv/bin/pio run -d firmware -e esp32s3 -t upload
~/.platformio/penv/bin/pio run -d firmware -e esp32s3 -t monitor

# ESP32-CAM (CH340 on /dev/ttyUSB0, 4 MB flash + 2 MB PSRAM)
~/.platformio/penv/bin/pio run -d firmware -e esp32cam
~/.platformio/penv/bin/pio run -d firmware -e esp32cam -t upload
~/.platformio/penv/bin/pio run -d firmware -e esp32cam -t monitor

# ESP32 DevKit (CP2102 on /dev/ttyUSB0, no PSRAM)
~/.platformio/penv/bin/pio run -d firmware -e esp32dev
~/.platformio/penv/bin/pio run -d firmware -e esp32dev -t upload
~/.platformio/penv/bin/pio run -d firmware -e esp32dev -t monitor
```

The `platformio.ini` file has a common `[env]` section and four envs:

- common: `platform = espressif32@7.1.3`, `min_spiffs.csv` (app0/app1 of
  1.875 MB, LittleFS of 128 KB), `littlefs`, and the libraries;
- `esp32c3`: `board = esp32-c3-devkitm-1`, USB CDC;
- `esp32s3`: `board = esp32-s3-devkitc-1`, `board_upload.flash_size = 4MB`,
  `board_build.arduino.memory_type = qio_qspi`, and `-DBOARD_HAS_PSRAM`;
- `esp32cam`: `board = esp32cam`, upload 460800, `-DBOARD_HAS_PSRAM`,
  `MAX_ACTIVE_CAMERAS=2`, and default name `x5bridge-cam`;
- `esp32dev`: `board = esp32dev`, upload 115200, `MAX_ACTIVE_CAMERAS=1`, and
  default name `x5bridge-dev`.

The `upload_port`/`monitor_port` use `/dev/serial/by-id/...` on the C3/S3 and
`/dev/ttyUSB0` on the CAM/DevKit. CAM and DevKit share `/dev/ttyUSB0`; connect
one at a time. The firmware uses ~0.93–1.00 MB (≈48–51% of the app slot). The
final `.bin` is in `.pio/build/<env>/firmware.bin` and is also copied to
`firmware/firmware.bin`.

## First boot and configuration

1. On the first boot, the device starts an open AP: **`x5bridge`** (C3/S3),
   **`x5bridge-cam`** (ESP32-CAM), or **`x5bridge-dev`** (DevKit).
2. Connect a cell phone or notebook to this AP and open `http://192.168.4.1`.
3. Select the WiFi network and enter the password.
4. The device reboots and responds at `http://x5bridge.local/`,
   `http://x5bridge-cam.local/`, or `http://x5bridge-dev.local/`.

The configuration is in NVS (`Preferences`, namespace `x5bridge`). The
WiFiManager portal handles only WiFi; the page configures the rest.

### `/config`

- `device_name` — device name and MQTT topic root (default `x5bridge` on the
  C3/S3, `x5bridge-cam` on the CAM, and `x5bridge-dev` on the DevKit),
  sanitized to `[A-Za-z0-9_-]`;
- MQTT broker: IP, port, user, and password;
- 4 camera slots (`CAM0`..`CAM3`): name, IP, resolution (`640x480` or
  `320x240`), and enabled; 320x240 cuts the bitrate ~4x and is the main lever
  against UDP packet loss;
- fixed port for each slot: `CAM0`=8080, `CAM1`=8081, `CAM2`=8082, `CAM3`=8083;
- active cameras: C3=**2**, S3=**4**, CAM=**2**, DevKit=**1**.

When you save, the device reboots to apply the configuration. The reboot is
scheduled (1.5 s) so that the server can send the HTTP response.

### `/discover`

Sends `LanSearch` by broadcast and by subnet broadcast, collects the `PunchPkt`
for ~2 s, and lists the cameras with name (from the packet) and IP. The
assignment to a slot saves the IP, the name, and the `punch`, then reboots the
device.

If the camera is busy (for example, with `linux/serve` or the official app), it
can fail to answer the `LanSearch` and does not appear in the list.

## Endpoints

| Port | Path | Function |
|---|---|---|
| 80 | `/` | status: version, IP, MAC, uptime, heap, RSSI, broker, cameras (frames, drop, recovered, FPS), and links |
| 80 | `/config` | configuration (GET/POST) |
| 80 | `/discover` | camera discovery |
| 80 | `/info` | ESP32, FS, and configuration dumps |
| 80 | `/logs` | logs (RAM + `/system.log` in LittleFS); `?clear=1` clears them |
| 80 | `/update` | OTA multipart `.bin` |
| 80 | `/reboot` | reboot |
| 80 | `/reset` | erase LittleFS, configuration, and WiFi (not tested in production) |
| 8080+ | `/video` | continuous MJPEG |
| 8080+ | `/snapshot` | single JPEG |

`/video` responds with `multipart/x-mixed-replace; boundary=x5frame`. The first
frame is already complete. `/snapshot` responds `503` while there is no frame.

## MQTT

The client uses `PicoMQTT::Client`. The root is `device_name` (default
`x5bridge`) and the announcement repeats every 5 s
(`MQTT_ANNOUNCE_INTERVAL_MS`).

```
<root>/DESCRIPTION   <root>/IP          <root>/STATUS
<root>/ADMIN         <root>/MAC         <root>/UPTIME
<root>/HEAP          <root>/RSSI
<root>/CAM0/DESCRIPTION  <root>/CAM0/IP       <root>/CAM0/STATUS
<root>/CAM0/PORT         <root>/CAM0/URL      <root>/CAM0/SNAPSHOT
<root>/CAM0/CLIENTS      <root>/CAM0/FPS      <root>/CAM0/LAST_FRAME
```

- `DESCRIPTION` = `<device_name> <VERSION>`, with version, date, hash, and branch
  generated at build time.
- `STATUS` = `ONLINE`; the LWT publishes `OFFLINE`.
- `CAMn/URL` and `CAMn/SNAPSHOT` already point to the current IP.
- The tested broker (`192.168.0.250`) does not keep `retained`; thus the short
  periodic announcement. On a broker that keeps `retained`, the topics work as
  retained.

## Architecture

### Tasks

- **`camera_task`** (priority 5, core 0): UDP sockets for each camera, `poll`,
  ACK for each packet, keepalive (`P2PAlive`), ticket extraction, VPS/SV send,
  and JPEG assembly. Timeout of 5 s with no packets → re-login. If the ticket
  is valid but no frame arrives for 15 s (keepalives still keep the session
  "online"), the session is restarted (media-stall recovery).
- **`stream_task`** (priority 4, core 1 on the S3/CAM/DevKit and core 0 on the
  C3): TCP listeners 8080+, minimal HTTP parser, send with socket timeout (1 s),
  and client drop/limit.
- **Main loop**: `handle_web()` (mDNS is part of the stack) and `handle_mqtt()`;
  checks the scheduled reboot.
- **Async web server** and **PicoMQTT** run in their own tasks.

### Memory and buffers

- Each active camera allocates **2 × 24 KB** (assembly + last JPEG).
- On the S3 and ESP32-CAM, these buffers go to **PSRAM** (fallback to internal
  RAM); on the C3/DevKit, they stay in internal RAM.
- `stream_task` uses a shared buffer of **24 KB** to send without holding the
  camera mutex.
- Limits: `MAX_ACTIVE_CAMERAS` = C3 **2**, S3 **4**, CAM **2**, DevKit **1**;
  `MAX_CLIENTS_PER_STREAM = 2`.
- C3 with one camera + one client: heap ~70–77 KB free (min. ~50 KB).
- S3 with one camera + one client: heap ~200 KB free and PSRAM ~2 MB free.
- ESP32-CAM with one camera + one client: heap ~146–158 KB free and PSRAM
  ~2.05 MB (buffers in PSRAM).
- ESP32 DevKit with one camera: heap ~107 KB free (min. ~86 KB), buffers in
  internal RAM.
- The second camera starts only if the allocation fits; if not, the slot stays
  offline with a `BUFFER FAIL` log.

### Camera session

Flow implemented in `camera.cpp`:

```
LanSearch -> PunchPkt -> P2pRdy -> login 180B -> DrwAck
          -> ConnectUserAck (ticket) -> VideoParamSet + StartVideo
          -> stream=1 (ACK for each packet) + P2PAlive
```

The 180-byte login is hardcoded (copied from the official app). The session does
not end when there is no viewer; the camera continues to send and the firmware
keeps the last frame for `/snapshot` and for new clients.

### Packet loss handling

Media packets carry an incremental `id` (bytes 6–7). The assembly tracks the
expected next `id` per camera:

- forward jumps are counted as lost packets (the gap is reflected in the loss
  percentage);
- late/duplicate packets (lower `id`) are dropped instead of being appended out
  of order, which would corrupt the JPEG.

A frame is only published when the JPEG is complete (`FF D8`..`FF D9`) **and**
the estimated loss is at most `CAMERA_FRAME_LOSS_LIMIT` (30%). Frames above the
limit are counted in the `Drop` column of `/` and the previous good frame is
kept. This avoids showing heavily corrupted frames without zeroing the FPS when
the loss is small. WiFi power save is disabled so the radio does not sleep
between packets, and each camera socket requests a 32 KB receive buffer.

If a camera loses the `type=03` frame markers (some models send them sparsely),
several JPEGs would be merged into one buffer and dropped. As a fallback
(`CAMERA_SOI_FALLBACK`), a raw packet whose payload starts with `FF D8` closes
the previous frame and starts a new one. The `Recov` column of `/` counts how
many boundaries were recovered this way.

## Versioning

- `buildscript_versioning.py` runs as `pre:` and:
  - increments the `versioning` file;
  - generates `include/version.h` with `VERSION`, `BUILD_NUMBER`, and
    `VERSION_SHORT` in the format `v0.1.N - date [hash|branch]`.
- `copy_firmware.py` runs as `post:` and copies the `.bin` to the root of
  `firmware/`.
- `include/version.h` and `firmware.bin` are ignored by git.

## Validation

Run with camera `BATE558596HDLZB` at `192.168.0.10`:

| Item | C3 | S3 | CAM | DevKit |
|---|---|---|---|---|
| Build/flash | OK (~0.98 MB) | OK (~0.93 MB) | OK (~1.00 MB) | OK (~0.99 MB) |
| WiFi + portal + mDNS | OK | OK | OK | OK |
| PSRAM + dual-core | — | OK (2 MB) | OK (2 MB) | — |
| `discover` | OK | OK | OK | OK |
| `/video` and `/snapshot` | OK | OK | OK | OK |
| MQTT | OK | OK | OK | OK |
| OTA `.bin` | OK | OK | OK | OK |
| `/config` + reboot | OK | OK | OK | OK |
| `/reboot` | OK | OK | OK | OK |
| 10 min soak | OK, no reboot | not repeated | not repeated | not repeated |
| `/reset` | not tested | not tested | not tested | not tested |
| 2 cameras | not tested | partial (CAM1 OK; CAM0 stalled) | OK (low FPS) | not tested |

Measurements:

- native camera (Linux `session`): ~11.9 fps (358 frames/30 s);
- C3 (RSSI ~-50 dBm): ~10 fps with no client; ~7.7 fps with 1 client; heap ~87
  KB free (min. ~50 KB);
- S3 (RSSI ~-64/-65 dBm): ~9 fps with no client; ~7.5 fps with 1 client; heap
  ~204 KB free and PSRAM ~2 MB;
- ESP32-CAM (RSSI ~-57/-58 dBm): ~7.7 fps with no client; ~6.8 fps with 1
  client; heap ~158 KB free and PSRAM ~2.05 MB;
- ESP32 DevKit (RSSI ~-48 dBm): ~5.9 fps with no client; ~4.3 fps with 1 client;
  heap ~161 KB idle / ~107 KB with camera (min. ~86 KB);
- 2 cameras on the ESP32-CAM (RSSI ~-57/-58): CAM0 4.07/2.03 fps and CAM1
  6.73/6.13 fps (idle/1 client); heap ~148 KB (min. ~115 KB), PSRAM ~2 MB;
- 2 cameras on the S3: CAM1 `.71` ~9–15 fps initial; CAM0 `.10` ONLINE with 0
  frames (stalled session);
- external antenna of the ESP32-CAM: RSSI did not improve (-58/-61 dBm);
- JPEG ~18–22 KB;
- soak on the C3: 601→6103 frames (~9.6 fps average), stable heap.

The ~16 fps from the original doc did not reproduce on this camera; the current
mode gives ~12 fps. On the S3, the FPS did not increase because the RSSI was
~15 dB worse than the C3; the limiter was WiFi, not CPU/RAM. On the CAM/DevKit,
the classic ESP32 radio limits the FPS; the CAM PSRAM helps memory, but not the
radio.

## Limitations and open items

- Admin/OTA open, without authentication (design decision; LAN).
- `/reset` implemented, but not tested in production.
- Reconnection after a camera power loss was not tested explicitly.
- 2-camera test: ESP32-CAM works but with low FPS (classic radio); S3 partial
  (CAM1 `.71` OK, CAM0 `.10` stalled). Limit: C3=2, S3=4, CAM=2, DevKit=1; each
  stream accepts 2 clients.
- Camera `.10` can stay ONLINE with a ticket and with **0 frames** after bridge
  changes; a power cycle and a clean re-login solve it. The `.71` also needed a
  power cycle one time. The media-stall detection (15 s) redoes the handshake
  automatically, but a camera already wedged may still need a power cycle.
- Camera model matters: `.10` (`BATE558596HDLZB`) emits only ~2–3 fps in
  bursts, even with motion in front of it; `.71` (`BATE583547ANCGG`, on the
  bench next to the AP) stays at ~10–15 fps with ~0% loss. The bridge cannot
  exceed the camera's own frame rate.
- External antenna of the ESP32-CAM: RSSI did not improve (-58/-61); check the
  PCB/external selection.
- Broker without `retained` in the test.
- FPS below the value in the original document (~12 fps native on this camera).
- S3 tested with RSSI ~-64/-65 dBm against ~-50 dBm for the C3; the FPS was
  limited by WiFi. With a better antenna/position, the S3 should approach the
  ceiling (~12 fps).
- ESP32-CAM: the onboard camera of the board used as the bridge is broken and
  is not used; the bridge uses the external X5 camera. The classic ESP32 radio
  limits the FPS (~7.7/6.8). See
  [`../docs/esp32cam-boards.md`](../docs/esp32cam-boards.md).
- ESP32 DevKit: no PSRAM, 1 active camera, and ~5.9/4.3 fps; use it as a test
  bench, not as a performance target.

## Troubleshooting

- **Portal does not appear**: wait for the connection timeout; the AP is
  `x5bridge` (C3/S3), `x5bridge-cam` (CAM), or `x5bridge-dev` (DevKit). If
  necessary, reset the configuration and WiFi with `/reset`.
- **Camera does not appear in `discover`**: make sure that it is on the same
  subnet and that `linux/serve`, `linux/session`, or the official app does not
  use it.
- **`/video` opens but shows no image**: check `/snapshot`; if it returns `503`,
  the camera has not produced a frame yet (login/ticket).
- **MQTT does not connect**: confirm the broker/port/user/password at `/config`
  and that the broker accepts anonymous connections.
- **OTA fails**: use `.bin` (not `.gz`); the ESP32 `Update` does not decompress
  gzip.
- **S3 with low FPS/bad RSSI**: check the antenna (PCB vs external/jumper) and
  move closer to the AP; the tested S3 was ~15 dB worse than the C3.
- **ESP32-CAM/DevKit do not appear**: both use `/dev/ttyUSB0`; connect one at a
  time. CAM upload at 460800 and DevKit upload at 115200.
- **ESP32-CAM reboots/boot loop**: check the onboard camera flat/ribbon and the
  5 V supply; the bridge does not use the onboard camera. See
  [`../docs/esp32cam-boards.md`](../docs/esp32cam-boards.md).
- **Camera ONLINE with 0 frames**: stalled video session. The firmware detects
  15 s without frames and restarts the session; if it does not recover, power
  cycle the camera and, if possible, disable/enable the slot after it answers
  the `discover`.
- **One camera much slower than the other**: compare the model suffix in the
  punch (`/config` shows it) and the frame size. A remote unit can be limited
  by its own model/link: in the final test `BATE558596HDLZB` (`.10`) did ~2–3
  fps even at 320x240 and with motion, while `BATE583547ANCGG` (`.71`) did
  ~10–15 fps. Try 320x240, then move/repower the unit; the bridge cannot create
  frames the camera does not send.
- **Frames with `Recov` increasing**: the `type=03` marker was lost and the
  assembler resynchronized on the JPEG `FF D8`. That is expected and better
  than merging frames.
- **Two cameras on the ESP32-CAM**: it works, but the classic radio saturates;
  the total FPS stays at ~1 camera and CAM0 drops more with a client.
- **External antenna on the ESP32-CAM**: if the RSSI does not change, check the
  PCB/external selection (jumper/resistor) and the u.FL contact.
- **Do not run `linux/serve`/`linux/session` together with the bridge**: the
  camera serves one session at a time.

## Related documentation

- [`../docs/protocol.md`](../docs/protocol.md) — protocol.
- [`../docs/esp32-bringup.md`](../docs/esp32-bringup.md) — bring-up and
  measurements.
- [`../docs/esp32cam-boards.md`](../docs/esp32cam-boards.md) — comparison of the
  three ESP32-CAM boards.
- [`../linux/README.md`](../linux/README.md) — Linux reference.
