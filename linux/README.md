# Linux reference

Reference implementation of the X5 protocol in C++ for Linux. It was used to
validate the reverse engineering before the port to the ESP32-C3 and is still
useful for diagnosis.

## Files

- `session.cpp` — does discovery and login, prints the ticket, and saves the
  JPEGs to `frame0.jpg`, `frame1.jpg`, ...; it runs for 30 s.
- `serve.cpp` — starts an HTTP server with `/video` (MJPEG) and `/snapshot`
  (JPEG) and keeps the session with the camera.
- `Makefile` — builds the two binaries.

## Build

```bash
make -C linux
```

Requirements: `g++` with C++11/`pthread` and POSIX headers (Linux). The binaries
`linux/serve` and `linux/session` are ignored by git.

## `session`

```bash
./linux/session <camera-ip>
```

Example:

```bash
cd /tmp
/home/keres/Dropbox/WORK/x5-bridge-cpp/linux/session 192.168.0.10
```

Typical output: LAN search logs, `PunchPkt`, ticket, `VideoParamSet`,
`StartVideo`, JPEG count, and, at the end, `got_ticket=1 jpegs=N`.

The `frame*.jpg` files are written to the current directory. Use a temporary
directory to keep the repository clean:

```bash
mkdir -p /tmp/x5frames && cd /tmp/x5frames
/home/keres/Dropbox/WORK/x5-bridge-cpp/linux/session 192.168.0.10
```

## `serve`

```bash
./linux/serve <camera-ip> [http-port]
```

Example:

```bash
./linux/serve 192.168.0.10 8080
```

Then open:

- `http://localhost:8080/video`
- `http://localhost:8080/snapshot`

`serve` keeps the session open and reassembles the JPEGs; the browser receives
MJPEG in `multipart/x-mixed-replace`.

## Warning

The camera serves **one session at a time**. Do not run `serve`/`session` while
the ESP32-C3 firmware uses the camera (the bridge keeps the session alive). If
the camera is busy, the `LanSearch` can fail to answer and the program ends with
`!! no PunchPkt`.

To release the camera, stop the process that uses it or disable the slot in the
bridge (`/config` → clear `enabled`) and reboot.

## Protocol

The details of discovery, handshake, login, ticket, encryption, and media are in
[`../docs/protocol.md`](../docs/protocol.md).
