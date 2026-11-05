# X5 Bridge (YunSync / 2BK-2)

Bridge for **X5 (YsxLite)** cameras. The bridge is based on reverse engineering
of the proprietary P2P protocol. The repository contains the protocol
documentation, a working firmware for **ESP32-C3, ESP32-S3, ESP32-CAM and ESP32
DevKit**, and a reference implementation for Linux.

The firmware provides:

- camera discovery on the LAN;
- login/ticket and a continuous P2P session;
- MJPEG video at `/video` and JPEG at `/snapshot` (one port for each camera);
- a web page for status, configuration, discovery, logs and OTA, including a
  per-camera resolution selector (`640x480` or `320x240`);
- packet-loss-aware JPEG assembly (id/gap tracking, `FF D8` resynchronization)
  and media-stall recovery;
- MQTT announcement in the same format as the other devices in the house;
- mDNS (`x5bridge.local`) and a captive portal for WiFi configuration.

## Status

- Firmware validated on **ESP32-C3** (v0.1.10), **ESP32-S3** (v0.1.27),
  **ESP32-CAM** (v0.1.26) and **ESP32 DevKit** (v0.1.23) with camera
  `BATE558596HDLZB` at `192.168.0.10`.
- C3: heap ~87 KB free, ~10 fps with no client and ~7.7 fps with 1 client.
- S3: 2 MB PSRAM active, heap ~204 KB free, frame buffers in PSRAM, and tasks
  on both cores; ~9 fps with no client and ~7.5 fps with 1 client.
- ESP32-CAM (board 3, onboard camera broken and not used): 2 MB PSRAM, heap
  ~158 KB, buffers in PSRAM; ~7.7 fps with no client and ~6.8 fps with 1
  client.
- ESP32 DevKit (no PSRAM): heap ~161 KB idle / ~107 KB with camera; ~5.9 fps
  with no client and ~4.3 fps with 1 client.
- Test with **2 cameras**: on the ESP32-CAM both cameras work, but the classic
  radio saturates (CAM0 4.07/2.03 fps; CAM1 6.73/6.13 fps idle/client). On the
  S3, CAM1 (`.71`) worked well; CAM0 (`.10`) stayed ONLINE with a ticket but 0
  frames, and needed a power cycle / clean sequence.
- **Final 2-camera S3 test (320x240)**: CAM1 `BATE583547ANCGG` (`.71`, on the
  bench next to the AP) ran at ~10–15 fps with ~0% packet loss. CAM0
  `BATE558596HDLZB` (`.10`, remote) stayed at ~2–3 fps even with a person moving
  in front of it, in bursts, with 5–20% loss per frame. This points to the
  camera model/link on `.10`, not the bridge. See
  [`firmware/README.md`](firmware/README.md#packet-loss-handling).
- The external antenna of the ESP32-CAM did not improve the RSSI in the test
  (-58/-61 dBm); the PCB/external selection is probably not active.
- The S3 had RSSI ~-64/-65 dBm against ~-50 dBm for the C3; the FPS was limited
  by WiFi, not by CPU/RAM. The ESP32-CAM/DevKit use the classic ESP32 radio and
  are naturally below the C3/S3.
- MJPEG streaming, snapshot, MQTT, OTA, `discover`, `/config` and `/reboot` were
  tested on all four targets.
- 10 min soak on the C3 with no reboot and with the camera online the whole
  time; not repeated on the others.
- `/reset` not tested, to avoid erasing WiFi/config.
- 2-camera test done on the ESP32-CAM and partially on the S3; see details
  below. Camera `.10` can stay ONLINE with a ticket and 0 frames after bridge
  changes; a power cycle and a clean re-login solve it.

## Repository map

| Path | Content |
|---|---|
| [`firmware/`](firmware/) | PlatformIO firmware (C3, S3, ESP32-CAM, ESP32 DevKit) |
| [`linux/`](linux/) | Linux reference (`serve.cpp`, `session.cpp`, `Makefile`) |
| [`docs/protocol.md`](docs/protocol.md) | complete protocol reverse engineering |
| [`docs/esp32-bringup.md`](docs/esp32-bringup.md) | hardware measurements and bring-up |
| [`docs/esp32cam-boards.md`](docs/esp32cam-boards.md) | comparison of the three ESP32-CAM boards |
| [`tools/esp-probe/`](tools/esp-probe/) | reusable diagnostic firmware |
| [`firmware/README.md`](firmware/README.md) | build, configuration, endpoints and architecture |
| [`linux/README.md`](linux/README.md) | how to build and use the Linux reference |

## Quick start

### 1. Build and flash

```bash
# ESP32-C3
~/.platformio/penv/bin/pio run -d firmware -e esp32c3
~/.platformio/penv/bin/pio run -d firmware -e esp32c3 -t upload

# ESP32-S3 (4 MB flash + 2 MB PSRAM)
~/.platformio/penv/bin/pio run -d firmware -e esp32s3
~/.platformio/penv/bin/pio run -d firmware -e esp32s3 -t upload

# ESP32-CAM (4 MB flash + 2 MB PSRAM, CH340 on /dev/ttyUSB0)
~/.platformio/penv/bin/pio run -d firmware -e esp32cam
~/.platformio/penv/bin/pio run -d firmware -e esp32cam -t upload

# ESP32 DevKit (D0WD-V3, no PSRAM, CP2102 on /dev/ttyUSB0)
~/.platformio/penv/bin/pio run -d firmware -e esp32dev
~/.platformio/penv/bin/pio run -d firmware -e esp32dev -t upload
```

The `platformio.ini` file has four envs (`esp32c3`, `esp32s3`, `esp32cam` and
`esp32dev`) with `min_spiffs.csv`, `littlefs`, and `upload_port`/`monitor_port`.
Adjust the paths if the boards appear on another port. On the S3, the env
enables `board_upload.flash_size = 4MB`,
`board_build.arduino.memory_type = qio_qspi`, and `-DBOARD_HAS_PSRAM`. On the
CAM, `board = esp32cam`, upload 460800, PSRAM, and `MAX_ACTIVE_CAMERAS=2`. On
the DevKit, `board = esp32dev`, upload 115200, no PSRAM, and
`MAX_ACTIVE_CAMERAS=1`. CAM and DevKit share `/dev/ttyUSB0`; connect one at a
time. The binary is also copied to `firmware/firmware.bin`.

### 2. First boot and WiFi

On the first boot, the device starts an open AP: **`x5bridge`** (C3/S3),
**`x5bridge-cam`** (ESP32-CAM), or **`x5bridge-dev`** (DevKit):

1. Connect a cell phone or notebook to this AP;
2. Open `http://192.168.4.1`;
3. Select the WiFi network, enter the password, and save.

The device reboots, joins the network, and responds at
`http://x5bridge.local/`, `http://x5bridge-cam.local/`, or
`http://x5bridge-dev.local/`. The IP also appears on the home page and on the
serial port (115200).

### 3. Camera

1. Open `http://<bridge-ip>/discover`;
2. The bridge sends `LanSearch` by broadcast and lists the cameras that it
   finds;
3. Assign the camera to a slot (`CAM0` → port 8080, `CAM1` → 8081, ...);
4. The device reboots and starts the P2P session.

You can also enter the IP manually at `/config`.

### 4. Video

| URL | Format |
|---|---|
| `http://<ip>:8080/video` | continuous MJPEG |
| `http://<ip>:8080/snapshot` | single JPEG |
| `http://<ip>:8081/video` | second camera (if configured) |

The resolution is fixed at **640x480**.

### 5. MQTT

The default broker is `192.168.0.250:1883` (configurable at `/config`). The
topic root is the `device_name` (default `x5bridge`):

```text
x5bridge/DESCRIPTION   x5bridge/IP          x5bridge/STATUS
x5bridge/ADMIN         x5bridge/MAC         x5bridge/UPTIME
x5bridge/HEAP          x5bridge/RSSI
x5bridge/CAM0/DESCRIPTION  x5bridge/CAM0/IP       x5bridge/CAM0/STATUS
x5bridge/CAM0/PORT         x5bridge/CAM0/URL      x5bridge/CAM0/SNAPSHOT
x5bridge/CAM0/CLIENTS      x5bridge/CAM0/FPS      x5bridge/CAM0/LAST_FRAME
```

The announcement repeats every 5 s; `STATUS` has the LWT `OFFLINE`. The broker
used in the test does not keep `retained` messages, thus the short period.

## Architecture summary

- **Camera:** one UDP socket for each session; `LanSearch` → `PunchPkt` →
  `P2pRdy` → login → ticket → `VideoParamSet` + `StartVideo` → media. The
  session stays alive (there is no stop command); if no packets arrive for 5 s,
  the firmware logs in again.
- **Video:** raw TCP server on 8080+ with a minimal HTTP parser. The JPEG frame
  is assembled in 24 KB buffers and sent with a drop/limit policy for slow
  clients.
- **Admin:** `ESPAsyncWebServer` on port 80, with `ESPAsyncWiFiManager` for the
  captive portal, mDNS, and `ESPAsyncHTTPUpdateServer` for OTA `.bin`.
- **MQTT:** `PicoMQTT` with periodic announcement and LWT.
- **Persistence:** `Preferences` (NVS) for the device name, broker, and camera
  slots; LittleFS for logs.
- **Targets:** the same code runs on the C3, S3, ESP32-CAM and ESP32 DevKit. On
  the S3 and CAM, the frame buffers go to PSRAM; on the C3/DevKit they stay in
  internal RAM. On dual-core, the camera runs on core 0 and the stream on core
  1; on the C3, everything runs on core 0. Active camera limit: C3=2, S3=4,
  CAM=2, DevKit=1.

Build details, endpoints, memory, and limitations are in
[`firmware/README.md`](firmware/README.md).

## Validation

| Item | C3 | S3 | CAM | DevKit |
|---|---|---|---|---|
| Build/flash | OK | OK | OK | OK |
| WiFi + portal + mDNS | OK | OK | OK | OK |
| PSRAM + dual-core | — | OK (2 MB) | OK (2 MB) | — |
| Camera `discover` | OK | OK | OK | OK |
| `/video` and `/snapshot` | OK | OK | OK | OK |
| MQTT | OK | OK | OK | OK |
| OTA `.bin` via `/update` | OK | OK | OK | OK |
| `/config` + reboot | OK | OK | OK | OK |
| `/reboot` | OK | OK | OK | OK |
| 10 min soak | OK | not repeated | not repeated | not repeated |
| `/reset` | not tested | not tested | not tested | not tested |
| 2 cameras at the same time | not tested | partial (CAM1 OK; CAM0 ~2–3 fps) | OK (low FPS) | not tested |

Measurements with one camera:

- native camera measured by Linux: **~11.9 fps**;
- **C3** (RSSI ~-50 dBm): ~10 fps with no client; ~7.7 fps with 1 client;
  heap ~87 KB free, minimum ~50 KB;
- **S3** (RSSI ~-64/-65 dBm): ~9 fps with no client; ~7.5 fps with 1 client;
  heap ~204 KB free and PSRAM ~2 MB (frame buffers in PSRAM);
- **ESP32-CAM** (RSSI ~-57/-58 dBm): ~7.7 fps with no client; ~6.8 fps with 1
  client; heap ~158 KB free and PSRAM ~2.05 MB (buffers in PSRAM);
- **ESP32 DevKit** (RSSI ~-48 dBm): ~5.9 fps with no client; ~4.3 fps with 1
  client; heap ~161 KB idle / ~107 KB with camera (min. ~86 KB), buffers in
  internal RAM;
- **2 cameras** on the ESP32-CAM (RSSI ~-57/-58): CAM0 4.07 fps idle / 2.03 with
  client; CAM1 6.73 / 6.13; heap ~148 KB (min. ~115 KB), PSRAM ~2.0 MB;
- **2 cameras** on the S3: CAM1 `.71` ~9–15 fps initial; CAM0 `.10` ONLINE with
  0 frames (stalled session);
- **2 cameras at 320x240** on the S3: CAM1 `.71` ~10–15 fps, ~0% loss; CAM0
  `.10` ~2–3 fps, 5–20% loss per accepted frame, unaffected by motion in front
  of the camera (measured over 50 s);
- external antenna of the ESP32-CAM: RSSI did not improve (-58/-61 dBm);
- JPEG ~18–22 KB; 10 min soak on the C3 with ~9.6 fps average.

The ~16 fps from the original document did not reproduce on this camera; the
current mode gives ~12 fps. On the S3, the FPS did not increase because the
RSSI was ~15 dB worse than the C3; the limiter was WiFi, not CPU/RAM. On the
CAM/DevKit, the classic ESP32 radio limits the FPS; the CAM PSRAM helps memory,
but not the radio.

## Limitations and open items

- Admin and OTA **without authentication** (design decision; LAN use).
- `/reset` not tested, to avoid breaking the current configuration.
- Reconnection after a camera power loss was not tested explicitly; the
  firmware has a timeout and re-login.
- 2-camera test: ESP32-CAM works but with low FPS (classic radio); S3 partial
  (CAM1 `.71` OK, CAM0 `.10` stalled). Limit: C3=2, S3=4, CAM=2, DevKit=1; each
  stream accepts 2 clients. With 1 camera + 1 client, the C3 minimum is ~50 KB.
- Camera `.10` can stay ONLINE (with a ticket) and with **0 frames** after
  bridge changes; a power cycle and a clean re-login solve it. The `.71` also
  needed a power cycle one time. The firmware now detects a media stall (15 s
  without frames) and redoes the handshake automatically, but a camera that is
  already wedged may still need a power cycle.
- Camera model matters: `.10` is `BATE558596HDLZB` and `.71` is
  `BATE583547ANCGG`. In the final test, `.10` emitted only ~2–3 fps in bursts
  regardless of scene motion, while `.71` stayed at ~10–15 fps. The bridge
  cannot raise the FPS above what the camera sends.
- `.10` frames are large for 320x240 (~14–18 KB), so its packets/frame and
  airtime are higher than `.71` (~9–13 KB). Lowering JPEG quality/bitrate would
  be the next lever, but the documented VPS payload only exposes the
  resolution.
- External antenna of the ESP32-CAM: RSSI did not improve (-58/-61); check the
  PCB/external selection.
- The broker at `192.168.0.250` does not keep `retained`; if you need this,
  change or adjust the broker.
- FPS below the value in the original document (~12 fps native on this camera).
- S3 tested with RSSI ~-64/-65 dBm against ~-50 dBm for the C3. With a better
  antenna/position, the S3 should approach the camera ceiling (~12 fps).
- ESP32-CAM: board 3 has a broken onboard camera (not used); the bridge uses
  the external X5 camera. The classic ESP32 radio limits the FPS (~7.7/6.8).
- ESP32 DevKit: no PSRAM, 1 active camera, and ~5.9/4.3 fps; use it as a test
  bench, not as a performance target.
- Do not run two bridges on the same camera; the camera serves one session at a
  time.

## Documentation

- [`docs/protocol.md`](docs/protocol.md) — complete protocol (discovery,
  handshake, login, ticket, encryption, VPS/SV, media).
- [`docs/esp32-bringup.md`](docs/esp32-bringup.md) — C3/S3/CAM/DevKit
  measurements and port decisions.
- [`docs/esp32cam-boards.md`](docs/esp32cam-boards.md) — comparison of the three
  ESP32-CAM boards and the bridge board selection.
- [`firmware/README.md`](firmware/README.md) — firmware.
- [`linux/README.md`](linux/README.md) — Linux reference.

## References

- [cam-reverse](https://github.com/DavidVentura/cam-reverse) — original reverse
  engineering of the app.
- [YunSync / 2BK-2](https://github.com/DavidVentura/cam-reverse/issues) —
  protocol discussions.
