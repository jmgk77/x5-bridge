# X5 camera protocol (YunSync / 2BK-2) — reverse engineering

Documentation of the P2P protocol used by X5 cameras (YsxLite brand), based on:

- Reverse engineering of the Android app (`DavidVentura/cam-reverse`)
- PCAPdroid captures of the official app (working session)
- tcpdump captures of the TypeScript reimplementation
- C++ reimplementation (`../linux/serve.cpp`, this repository)

All information here was **observed empirically**. Fields marked with `?` are
unknown but work as constants; fields without `?` were decoded with certainty.

## Architecture

~~~
X5 camera (192.168.0.10:32108 UDP) <---> Client (Linux, ESP32, ...)
~~~

All traffic is **UDP**. The camera listens on port 32108. The client uses any
ephemeral port. There is no TCP, TLS, RTP, or RTSP. The "encryption" is a
trivial byte transformation (see the XqBytesEnc section).

One video "frame" on the wire (~16 KB of JPEG) is fragmented into ~25 UDP
packets of 1032 bytes each. ~16 fps → ~400 media packets/s.

## Complete flow

~~~
Client                                Camera
  |--- LanSearch (4B) ---------------->|
  |<-- PunchPkt (24B) -----------------|
  |--- P2pRdy (24B, echo) -------------|
  |<-- P2pRdy (24B) -------------------|
  |--- Drw login (180B) ---------------|
  |<-- DrwAck id=0 --------------------|
  |<-- Drw ConnectUserAck (28B) -------|  ticket extracted here (offset 24-27 after decrypt)
  |--- Drw VideoParamSet (28B) ------->|
  |--- Drw StartVideo (20B) ---------->|  (sent in sequence, without waiting for ACK)
  |<-- DrwAck id=1 --------------------|  (VPS ack)
  |<-- DrwAck id=2 --------------------|  (SV ack)
  |<-- Drw cmd=0x1831 (VPS ack) -------|  (stream=0)
  |--- DrwAck id=1 ------------------->|
  |<-- Drw cmd=0x1031 (SV ack) --------|  (stream=0)
  |--- DrwAck id=2 ------------------->|
  |                                    |
  |<== Drw stream=1 (media) ===========|  JPEGs continuously
  |--- DrwAck ------------------------>|  for each packet received
  |                                    |
  |--- P2PAlive ---------------------->|  (keepalive ~1 Hz)
  |<-- P2PAlive -----------------------|
  |--- P2PAliveAck ------------------->|
~~~

## 1. Discovery

### LanSearch (client → camera)

4 bytes, no header:

~~~
f1 30 00 00
~~~

### PunchPkt (camera → client)

24 bytes. Identifies the camera and its serial number:

~~~
f1 41 00 14 42 41 54 45 00 00 00 00 00 08 86 04 48 44 4c 5a 42 00 00 00
      ^^^^^ ^^^^^ ^^^^^^^^^^^^^^^^^^^^ ^^^^^^^^^^^^^^^^^^^
      len=20 "BATE" (serial)          "HDLZB" (model/suffix)
~~~

- The complete serial number is `BATE558596HDLZB` (16 bytes after `f1 41 00 14`)
- Bytes `00 00 00 08 86 04` in the middle are unknown but constant for each
  camera
- The device name is "BATE" + suffix

## 2. Handshake

### P2pRdy (client → camera)

Echo of the PunchPkt with **one byte changed** (byte[1] from `0x41` → `0x42`):

~~~
f1 42 00 14 42 41 54 45 ... (same 24 bytes, only 0x41 becomes 0x42)
~~~

The camera responds with its own `P2pRdy` (same format, also `f1 42...`). After
this, the camera is ready to receive the login.

### Keepalive

- **P2PAlive**: `f1 e0 00 00` — the camera sends it every ~1s. The client
  responds.
- **P2PAliveAck**: `f1 e1 00 00` — response to P2PAlive.

If the client does not respond in ~3s, the camera ends the session.

## 3. Drw header (common to all control commands)

Every Drw has the same 20-byte header structure:

~~~
offset  size  name            notes
------  ----  --------------  -------------------------------------------
0       2     hdr             always f1 d0
2       2     length          BE; total packet size - 4
4       1     param           always d1 (unlocked)
5       1     stream          0 = control, 1 = media
6       2     id              BE; command ID (incremental for each session)
8       2     start_cmd       always 11 0a
10      2     command         BE; command code (see table)
12      2     payload_len     u16_swap(4 + data_len); see note
14      2     cc_dest         BE; destination (see command table)
16      4     ticket          4 bytes; zeros until ConnectUserAck
20      ...   data            command-specific payload
~~~

**Note about payload_len**: the value is `u16_swap(4 + data_len)`, where `4` is
the size of the ticket. Example: for VPS with 8 bytes of data, payload_len = 12,
stored as `0c 00`.

**Command table** (`command` field):

| Command          | Value   | cc_dest |
|------------------|---------|---------|
| ConnectUser      | `20 10` | `ff 00` |
| ConnectUserAck   | `20 11` | `ff 00` |
| VideoParamSet    | `18 30` | `00 00` |
| VideoParamSetAck | `18 31` | `00 00` |
| StartVideo       | `10 30` | `00 00` |
| StartVideoAck    | `10 31` | `00 00` |
| (other acks)     | `18 32`, `10 32` | `00 00` |

The `*Ack` commands (`0x2011`, `0x1831`, `0x1031`) come **from the camera to the
client**, not the other way.

## 4. Login (ConnectUser)

### Login Drw (client → camera)

**Packet of 180 bytes total**, verified empirically. Structure:

- Drw header (20B): `f1 d0 00 b0 d1 00 <id:2> 11 0a 20 10 a4 00 ff 00`
- Ticket (4B): `00 00 00 00` (zeros — the login does not use a ticket)
- Payload (160B): fixed, **copied verbatim from the YsxLite app**

Complete hex (this works as a login):

~~~
f1d000b0d1000000110a2010a400ff00000000006f0101010101010101010101010101
01010101010101010101010101010160656c686f010101010101010101010101010101
0101010101010101010101010101010101010101010101010101010101010101010101
0101010101010101010101010101010101010101010101010101010101010101010101
0101010101010101010101010101010101010101010101010101010101010101010101
0101010101010101010101010101010101010101010101010101010101010101010101
0101010101010101010101010101010101010101010101010101010101010101010101
01010101010160656c68
~~~

**Important**: the `SendUsrChk` command from `cam-reverse` (which produces a
56-byte Drw with encrypted "admin"+"admin") **does NOT work** with the X5. The
camera accepts it (sends Ack), but does not enter continuous streaming mode. The
official app sends 180 bytes with much more information (device ID, version,
nonce, capabilities). The payload above is the capture from the app and it
works. If the payload is identical between sessions, it can be hardcoded (this
is what we did).

### DrwAck (camera → client) — login ack

32 bytes:

~~~
f1 d1 00 06 d2 00 00 01 00 00 00 00 ...
^^^^ ^^^^^ ^^ ^^ ^^^^^ ^^^^^ ^^^^^
DrwAck len stream item=1 pkt_id=0 (id of the Drw being acked, BE)
~~~

**Important: the pkt_id offset**: in a normal Drw, the id is at [6..7]. In a
DrwAck, the id is at **[8..9]** (because [6..7] is `item_count = 1`). It is easy
to use the wrong offset.

### ConnectUserAck (camera → client) — 28 bytes, contains the ticket

Drw header (20B) + 8 bytes of **encrypted** payload. After decrypt:

~~~
offset  size  content
------  ----  -------------------------------------
20      4     status / not used
24      4     TICKET  <-- what we need
~~~

The extraction code (after XqBytesDec on the 8-byte payload):

~~~c
int payload_len = (buf[12] | (buf[13] << 8));  // LE (!)
int dec_len = payload_len - 4;
if (dec_len > 0) xq_dec(buf + 20, dec_len, 4);
memcpy(ticket, buf + 24, 4);
~~~

**The ticket changes for each session** (the camera generates it), so it must be
extracted for each login. After extraction, it is used in **all** subsequent
Drws (VPS, SV).

## 5. XqBytesEnc / XqBytesDec — the "encryption"

This is not real encryption. It has two operations:

1. **Invert bit 0** of each byte: odd → subtract 1, even → add 1
2. **Rotate** the buffer by N bytes (N=4 is the only value used)

~~~c
// Encrypts in-place (used in payloads that we SEND)
void xq_enc(uint8_t* buf, int len, int rot) {
    uint8_t* nb = malloc(len);
    for (int i = 0; i < len; i++) nb[i] = (buf[i] & 1) ? (buf[i]-1) : (buf[i]+1);
    for (int i = 0; i < len-rot; i++) buf[i] = nb[i+rot];      // "left" rotation
    for (int i = 0; i < rot; i++) buf[len-rot+i] = nb[i];
    free(nb);
}

// Decrypts in-place (used in payloads that we RECEIVE)
void xq_dec(uint8_t* buf, int len, int rot) {
    uint8_t* nb = malloc(len);
    for (int i = 0; i < len; i++) nb[i] = (buf[i] & 1) ? (buf[i]-1) : (buf[i]+1);
    for (int i = rot; i < len; i++) buf[i] = nb[i-rot];        // "right" rotation
    for (int i = 0; i < rot; i++) buf[i] = nb[len-rot+i];
    free(nb);
}
~~~

Note: the bit inversion is the **same** in both operations. The only difference
is the rotation direction. Be careful when you copy from another source; some
implementations rotate in the opposite direction and produce garbage.

## 6. VideoParamSet + StartVideo

Both use the **ticket** extracted from the ConnectUserAck. Send them **in
immediate sequence** (without waiting for an Ack between them). The official app
does this.

### VideoParamSet (client → camera) — 28 bytes

~~~
f1 d0 00 18 d1 00 <id:2> 11 0a 18 30 0c 00 00 00 <ticket:4> <payload:8>
^^^^ ^^^^^                            ^^^^^ ^^^^^ ^^^^^^^^^^^ ^^^^^^^^^^
hdr  len=24                           plen  cc=0    ticket      payload
~~~

**id** = 1 (the first id after the ConnectUserAck)
**payload** (8 bytes, before encryption): `01 00 00 00 02 00 00 00`

Meaning: `[resolution_id=1 (640x480), ...?, value=2]`. For 320x240, use
`01 00 00 00 01 00 00 00` (see the table in `impl.ts` of cam-reverse).

The payload **must be encrypted** with `xq_enc(payload, 8, 4)` before you put it
in the packet. On the wire, the payload (after the ticket) is:
`03 01 01 01 00 01 01 01`.

### StartVideo (client → camera) — 20 bytes

~~~
f1 d0 00 10 d1 00 <id:2> 11 0a 10 30 04 00 00 00 <ticket:4>
^^^^ ^^^^^                            ^^^^^ ^^^^^ ^^^^^^^^^
hdr  len=16                           plen=4 cc=0    ticket
~~~

**id** = 2. **No payload** (payload_len=4 is only the ticket).

### Camera acks

In response, the camera sends two packets with stream=0:

~~~
DrwAck id=1          (VideoParamSet ack)
DrwAck id=2          (StartVideo ack)
Drw cmd=0x1831       (VideoParamSet ack; stream=0)
Drw cmd=0x1031       (StartVideo ack; stream=0)
~~~

The client **must ACK all of them**, including the two `cmd=0x18xx/0x10xx`
packets (they are not DrwAcks, they are normal Drws). After this cycle, the
camera starts to send `stream=1`.

## 7. Media (stream=1)

### Packet format

~~~
offset  size  content
------  ----  --------------------------------------------------
0       2     f1 d0
2       2     length
4       1     d1
5       1     01  (stream=1)
6       2     id (BE, incremental)
8       4     magic: 55 aa 15 a8 (if "framed") OR raw data
~~~

There are **two types** of media packet:

#### "framed" packet (start of block)

~~~
offset  size  content
------  ----  --------------------------------------------------
8       4     55 aa 15 a8  (magic)
12      1     type: 03 = video, 06 = audio
13      ...   metadata (not decoded)
~~~

They typically appear with size 36-40 bytes. Type=03 comes before each new JPEG;
type=06 comes before each audio chunk.

They **do not** add bytes to the JPEG. The JPEG starts in the next packet.

#### "raw" packet (media data)

~~~
offset  size  content
------  ----  --------------------------------------------------
8       ...   raw data (for video: JPEG bytes, starting with ff d8 in the 1st packet)
~~~

Typical size: 1000-1032 bytes. Concatenate them to assemble the JPEG.

### Frame assembly

~~~
for each packet with stream=1:
    if it has magic and type=03:
        # a new frame started
        if current_buffer has ffd8 (SOI) and ff d9 (EOI) at the end:
            emit current_buffer as JPEG
        current_buffer = empty
        # add nothing from the framed packet
    else if it has magic and type=06:
        # audio, ignore
        continue
    else (raw):
        current_buffer += bytes[8:]
~~~

The JPEG ends with `ff d9` (EOI marker). Because of padding, the last packet
often has extra bytes after the `ff d9`. The working code **truncates at the
last `ff d9`** before it emits the frame:

~~~c
int end = -1;
for (int i = 0; i < len-1; i++) if (buf[i]==0xff && buf[i+1]==0xd9) end = i;
if (end > 0) emit(buf, end + 2);   // inclusive
~~~

### Observed metrics

- **FPS**: ~16 fps (640x480)
- **JPEG size**: 14-18 KB for each frame (average ~16 KB)
- **Packets for each frame**: ~25 (24 data + 1 framed)
- **UDP rate**: ~400 packets/s
- **Throughput**: ~260 KB/s of video

## 8. Notes for the ESP32 port

- **Sockets**: use `lwip_*` (POSIX-like API of ESP-IDF). `recvfrom`/`sendto`
  work directly.
- **Keepalive**: answer `P2PAlive` every 1s in <3s. Use `esp_timer` or a
  dedicated task.
- **HTTP MJPEG**: `esp_http_server` has no native support for
  `multipart/x-mixed-replace` streaming. Two options: (a) use
  `httpd_req_send_chunk()` manually (it works, but with workarounds); (b) use
  raw TCP sockets + a minimal HTTP parser (this is what we did in
  `../linux/serve.cpp`, ~80 lines).
- **Memory**: each JPEG has ~16 KB. For double buffering (send the previous
  frame while you assemble the next one), reserve ~40 KB of heap. An ESP32 with
  PSRAM is recommended for margin.
- **Hardcoded login**: the 180 login bytes go in flash (`.rodata`). Do not try
  to generate them dynamically; the real format is unknown.
- **Do not use TLS**: the protocol is all plaintext. The WiFi must be reliable
  (local network), which is acceptable for home use.
- **Video bandwidth**: 260 KB/s × 8 = ~2 Mbps. WiFi 802.11n can handle it. But
  if the camera is far from the AP, UDP packet loss breaks the complete frame.
  Implement a retry or accept that some frames will be missing.
- **Discovery**: `LanSearch` by UDP broadcast (destination
  `255.255.255.255:32108` or direct IP). On the ESP32, `SO_BROADCAST` works
  through `lwip`.
- **C++ on ESP-IDF**: `../linux/serve.cpp` uses
  `std::thread`/`std::mutex`/`std::vector`. On ESP-IDF it works with
  `CONFIG_COMPILER_CXX_EXCEPTIONS=y` and `pthread` enabled, but the
  recommendation is to replace them with
  `xTaskCreate`/`xSemaphoreCreateMutex` to reduce heap overhead.

## 9. Known limitations

- **Does not decode audio** (the protocol delivers it, but the payload format
  was not analyzed).
- **Does not send audio** (the app supports the phone microphone; not
  implemented).
- **Does not change the resolution** (only 640x480; the VPS format for other
  resolutions is in `impl.ts` of cam-reverse but not tested).
- **Does not send PTZ commands** (pan/tilt/zoom). Commands exist in
  `cam-reverse`, but they are not part of the capture used.
- **Fixed login**: the 180-byte login is from the official app and was copied
  verbatim. If it has a nonce that changes for each installation/session, this
  implementation can stop working on some camera firmware version. Tested on
  the X5 with firmware `BATE558596HDLZB`.

## 10. References

- [cam-reverse](https://github.com/DavidVentura/cam-reverse) — original reverse
  engineering of the app
- [YunSync / 2BK-2](https://github.com/DavidVentura/cam-reverse/issues) —
  protocol discussions
- Captures used: `~/Downloads/PCAPdroid_03_nov._17_10_12.pcap` (Android app),
  `~/Downloads/cam-reverse-trace-*.pcap` (Linux implementation)

## 11. Implementation

- ESP32-C3 firmware: [`../firmware/README.md`](../firmware/README.md)
- Hardware bring-up and measurements: [`esp32-bringup.md`](esp32-bringup.md)
- Linux reference: [`../linux/README.md`](../linux/README.md)
