# ESP32-CAM — comparison of the three boards

Record of the examination of three **AI-Thinker ESP32-CAM** modules to select
one for the X5 bridge. The bridge does not use the onboard camera. The objective
was to identify the board with the defective camera and reuse it.

## Method

The diagnostic firmware [`tools/esp-probe/`](../tools/esp-probe/) was flashed in
the `esp32cam` env (`-DCAMERA_PROBE`). The probe tests `camera_init`, reads the
`sensor_pid`, and tries **3 frames** at each resolution: QVGA 320x240, VGA
640x480, SVGA 800x600, and UXGA 1600x1200.

## Result

| Board | MAC | Chip | Flash | PSRAM | Camera | QVGA | VGA | SVGA | UXGA |
|---|---|---|---|---|---|---|---|---|---|
| 1 | `3C:E9:0E:86:3F:84` | ESP32-D0WD-V3 rev v3.0 | 4 MB | 2 MB | OV2640, PID `0x26`, init OK | OK 3/3 | OK 3/3 | 1 frame, then FAIL | FAIL |
| 2 | `A0:A3:B3:29:F9:2C` | ESP32-D0WD-V3 rev v3.1 | 4 MB | 2 MB | OV2640, PID `0x26`, init OK | OK 3/3 | OK 3/3 | 1 frame, then FAIL | FAIL |
| 3 | `A0:A3:B3:2C:6F:B4` | ESP32-D0WD-V3 rev v3.1 | 4 MB | 2 MB | OV2640, PID `0x26`, init OK | **FAIL** | **FAIL** | **FAIL** | **FAIL** |

## Conclusion

- **Board 3** has the defective camera: the sensor is detected, but it delivers
  **no frame** at any resolution.
- Boards 1 and 2 have the same behavior: they work well at VGA and fail at
  SVGA/UXGA. This looks like a common limitation of the OV2640/ESP32-CAM set
  used in the bring-up, not a defect that is exclusive to one board.
- **Board 3** was selected as the bridge, because the onboard camera is not
  used.

## Observation about the bridge on board 3

On the first flash of the bridge firmware, board 3 entered a boot loop and the
AP/`x5bridge-cam.local` did not stabilize. After we **reconnected the camera flat
ribbon** and flashed again, it ran stable:

- `CAM0` with the external X5 camera at `192.168.0.10`;
- frame buffers in PSRAM (2 MB);
- internal heap ~158 KB (min. ~141 KB);
- `/video`, `/snapshot`, MQTT, OTA, and `/reboot` validated;
- ~7.7 fps with no client and ~6.8 fps with 1 client (limited by the WiFi/UDP of
  the classic ESP32).

Because the onboard camera is not used, its defect does not affect the bridge.
If the board resets again, the first suspect is the camera module/ribbon or the
5 V supply.

## Test with two cameras (bridge on board 3)

- CAM0 `.10` (`BATE558596HDLZB`) and CAM1 `.71` (`BATE583547ANCGG`) online at the
  same time; valid `/snapshot` on ports 8080 and 8081; MQTT with `CAM0/...` and
  `CAM1/...`.
- PSRAM ~2.0 MB free after the 4 buffers (96 KB); heap ~148 KB (min. ~115 KB
  with 1 client on each).
- FPS in 30 s:
  - idle: CAM0 4.07; CAM1 6.73;
  - with 1 client for each camera: CAM0 2.03; CAM1 6.13.
- Conclusion: the classic ESP32 saturates; the total throughput stays similar
  to 1 camera and CAM0 drops more with a client. CAM1 recovered with a power
  cycle; CAM0 (`.10`) again stayed ONLINE with a ticket and 0 frames.

## External antenna

- The external antenna was connected/moved, but the RSSI stayed at
  -58/-60/-61 dBm, with no improvement. The PCB/external selection is probably
  not active or the u.FL connector is not making contact. The classic radio
  remains the limiter.

## S3 with two cameras (partial)

- CAM1 `.71` works well (~9–15 fps initial).
- CAM0 `.10` stays ONLINE with a ticket and 0 frames; the S3 itself is healthy
  (heap ~206 KB, PSRAM 2 MB, RSSI -54/-56).
- The problem is in `.10`/the session, not in the S3. The complete test of both
  cameras on the S3 is still open.

## Links

- [`../firmware/README.md`](../firmware/README.md) — firmware and targets.
- [`esp32-bringup.md`](esp32-bringup.md) — hardware bring-up and measurements.
- [`../tools/esp-probe/`](../tools/esp-probe/) — diagnostic firmware.
