#include "main.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

static const char LOGIN_HEX[] =
    "f1d000b0d1000000110a2010a400ff00000000006f01010101010101010101010101010101010101010101010101010160656c686f01010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010160656c68";

struct CameraSession {
  int fd;
  int slot;
  bool active;
  bool got_punch;
  bool got_ticket;
  uint8_t punch[24];
  char name[32];
  uint8_t ticket[4];
  struct sockaddr_in addr;
  uint32_t last_rx;
  uint32_t ticket_ms;
  uint32_t last_action;
  uint32_t frames;
  uint32_t dropped;
  uint32_t recovered;
  uint32_t last_frame_ms;
  uint32_t frame_seq;
  uint32_t fps_count;
  uint32_t fps_window_start;
  float fps;
  uint16_t frame_start_id;
  uint16_t media_last_id;
  uint16_t media_stride;
  uint32_t frame_rx;
  bool frame_open;
  uint8_t* assemble;
  size_t assemble_len;
  uint8_t* last_jpeg;
  size_t last_jpeg_len;
};

static CameraSession sessions[MAX_CAMERAS];
static SemaphoreHandle_t g_mutex = nullptr;
static volatile int g_stream_clients[MAX_CAMERAS] = {0};

static int hex2bin(const char* h, uint8_t* o, int m) {
  int n = strlen(h);
  if (n % 2) return -1;
  int b = n / 2;
  if (b > m) return -1;
  for (int i = 0; i < b; i++) {
    unsigned int v;
    if (sscanf(h + 2 * i, "%2x", &v) != 1) return -1;
    o[i] = (uint8_t)v;
  }
  return b;
}

static void xq_enc(uint8_t* buf, int len, int rot) {
  uint8_t* nb = (uint8_t*)malloc(len);
  for (int i = 0; i < len; i++)
    nb[i] = (buf[i] & 1) ? (buf[i] - 1) : (buf[i] + 1);
  for (int i = 0; i < len - rot; i++) buf[i] = nb[i + rot];
  for (int i = 0; i < rot; i++) buf[len - rot + i] = nb[i];
  free(nb);
}

static void xq_dec(uint8_t* buf, int len, int rot) {
  uint8_t* nb = (uint8_t*)malloc(len);
  for (int i = 0; i < len; i++)
    nb[i] = (buf[i] & 1) ? (buf[i] - 1) : (buf[i] + 1);
  for (int i = rot; i < len; i++) buf[i] = nb[i - rot];
  for (int i = 0; i < rot; i++) buf[i] = nb[len - rot + i];
  free(nb);
}

static uint16_t u16_swap(uint16_t x) { return (x >> 8) | (x << 8); }

static int make_data_read_write(uint8_t* out, uint16_t cmd, uint16_t id,
                                const uint8_t* ticket, const uint8_t* data,
                                int data_len) {
  int pkt_len = 0x10 + 0x4;
  int payload_len = 0x4;

  if (data && data_len > 4) {
    pkt_len += data_len;
    payload_len += data_len;
  }

  out[0] = 0xf1;
  out[1] = 0xd0;
  uint16_t l = pkt_len - 4;
  out[2] = (l >> 8) & 0xff;
  out[3] = l & 0xff;
  out[4] = 0xd1;
  out[5] = 0x00;
  out[6] = (id >> 8) & 0xff;
  out[7] = id & 0xff;
  out[8] = 0x11;
  out[9] = 0x0a;
  out[10] = (cmd >> 8) & 0xff;
  out[11] = cmd & 0xff;
  uint16_t pl = u16_swap(payload_len);
  out[12] = (pl >> 8) & 0xff;
  out[13] = pl & 0xff;
  out[14] = 0x00;
  out[15] = 0x00;
  memcpy(out + 16, ticket, 4);
  if (data && data_len > 4) memcpy(out + 20, data, data_len);
  return pkt_len;
}

static int make_drw_ack(uint8_t* out, uint8_t stream, uint16_t pkt_id) {
  memset(out, 0, 32);
  out[0] = 0xf1;
  out[1] = 0xd1;
  out[2] = 0x00;
  out[3] = 0x06;
  out[4] = 0xd2;
  out[5] = stream;
  out[6] = 0x00;
  out[7] = 0x01;
  out[8] = (pkt_id >> 8) & 0xff;
  out[9] = pkt_id & 0xff;
  return 32;
}

static void send_to_session(CameraSession& s, const void* data, size_t len) {
  if (s.fd < 0) return;
  sendto(s.fd, data, len, 0, (struct sockaddr*)&s.addr, sizeof(s.addr));
}

static void send_lan_search(int slot) {
  CameraSession& s = sessions[slot];
  uint8_t lan[4] = {0xf1, 0x30, 0x00, 0x00};
  send_to_session(s, lan, sizeof(lan));
  xlog_printf("CAM%d LANSEARCH %s\n", slot, eeprom.cameras[slot].ip);
}

static void send_p2prdy_and_login(int slot) {
  CameraSession& s = sessions[slot];
  if (!s.got_punch) return;

  uint8_t p2prdy[24];
  memcpy(p2prdy, s.punch, sizeof(p2prdy));
  p2prdy[1] = 0x42;
  send_to_session(s, p2prdy, sizeof(p2prdy));

  uint8_t login[256];
  int login_len = hex2bin(LOGIN_HEX, login, sizeof(login));
  if (login_len > 0) send_to_session(s, login, login_len);
  xlog_printf("CAM%d LOGIN SENT (%d B)\n", slot, login_len);
}

static void send_vps_sv(int slot) {
  CameraSession& s = sessions[slot];
  uint8_t res = eeprom.cameras[slot].resolution;
  if (res != CAMERA_RES_320X240 && res != CAMERA_RES_640X480) {
    res = DEFAULT_CAMERA_RESOLUTION;
  }
  uint8_t vp_payload[8] = {0x01, 0x00, 0x00, 0x00, res, 0x00, 0x00, 0x00};
  xq_enc(vp_payload, 8, 4);

  uint8_t pkt[64];
  int len = make_data_read_write(pkt, 0x1830, 1, s.ticket, vp_payload, 8);
  send_to_session(s, pkt, len);

  len = make_data_read_write(pkt, 0x1030, 2, s.ticket, NULL, 0);
  send_to_session(s, pkt, len);
  xlog_printf("CAM%d VPS+SV SENT\n", slot);
}

static uint32_t frame_loss_pct(uint16_t start_id, uint16_t next_id,
                               uint16_t stride, uint32_t rx) {
  if (stride == 0) return 100;
  uint16_t span = (uint16_t)(next_id - start_id);
  uint32_t slots = span / stride;
  uint32_t expected = slots > 0 ? slots - 1 : 0;
  if (expected == 0) return 100;
  uint32_t lost = expected > rx ? expected - rx : 0;
  uint32_t pct = (lost * 100u) / expected;
  return pct > 100 ? 100 : pct;
}

static void finalize_frame(int slot, uint32_t now, uint16_t next_framed_id) {
  CameraSession& s = sessions[slot];
  if (!s.frame_open) return;
  s.frame_open = false;

  uint32_t loss_pct = frame_loss_pct(s.frame_start_id, next_framed_id,
                                     s.media_stride, s.frame_rx);

  size_t frame_len = 0;
  if (s.assemble_len > 100) {
    int end_i = -1;
    for (size_t i = 0; i + 1 < s.assemble_len; i++) {
      if (s.assemble[i] == 0xff && s.assemble[i + 1] == 0xd9) end_i = i;
    }
    if (end_i > 0 && s.assemble[0] == 0xff && s.assemble[1] == 0xd8) {
      size_t len = end_i + 2;
      if (len <= MAX_JPEG_SIZE) frame_len = len;
    }
  }

  if (frame_len > 0 && loss_pct <= CAMERA_FRAME_LOSS_LIMIT) {
    camera_lock();
    memcpy(s.last_jpeg, s.assemble, frame_len);
    s.last_jpeg_len = frame_len;
    s.frame_seq++;
    s.frames++;
    s.last_frame_ms = now;
    s.fps_count++;
    if (now - s.fps_window_start >= CAMERA_FPS_WINDOW_MS) {
      s.fps = s.fps_count * 1000.0f / (float)(now - s.fps_window_start);
      s.fps_count = 0;
      s.fps_window_start = now;
    }
    camera_unlock();

    if (s.frames <= 3 || s.frames % 100 == 0) {
      xlog_printf("CAM%d JPEG %u (%u B) loss=%u%% fps=%.1f\n", slot,
                  (unsigned)s.frames, (unsigned)frame_len, (unsigned)loss_pct,
                  s.fps);
    }
  } else {
    s.dropped++;
    if (s.dropped <= 3 || s.dropped % 25 == 0) {
      xlog_printf("CAM%d FRAME DROP loss=%u%% rx=%u (%u B)\n", slot,
                  (unsigned)loss_pct, (unsigned)s.frame_rx,
                  (unsigned)frame_len);
    }
  }
}

static void start_frame(CameraSession& s, uint16_t id) {
  s.frame_start_id = id;
  s.media_last_id = id;
  s.frame_rx = 0;
  s.frame_open = true;
  s.assemble_len = 0;
}

static void process_media(int slot, const uint8_t* buf, int n, uint32_t now,
                          uint16_t id) {
  CameraSession& s = sessions[slot];
  bool is_framed = (n >= 13 && buf[8] == 0x55 && buf[9] == 0xaa &&
                    buf[10] == 0x15 && buf[11] == 0xa8);
  uint8_t stype = is_framed ? buf[12] : 0;

  if (is_framed && stype == 0x03) {
    finalize_frame(slot, now, id);
    start_frame(s, id);
    return;
  }

  if (!s.frame_open) return;

  uint16_t jump = (uint16_t)(id - s.media_last_id);
  if (jump == 0 || jump >= 0x8000) {
    return;
  }
  if (s.media_stride == 0 || jump < s.media_stride) {
    s.media_stride = jump;
  }
  s.media_last_id = id;

  if (is_framed) {
    s.frame_rx++;
    return;
  }
  if (n <= 8) return;

#if CAMERA_SOI_FALLBACK
  bool soi = n >= 10 && buf[8] == 0xff && buf[9] == 0xd8;
  // A new JPEG begins mid-assembly: the type=03 marker was lost (or the
  // previous frame never started with SOI). Close/discard the current buffer
  // and resynchronize on this packet instead of merging several JPEGs.
  if (soi && s.assemble_len > 100) {
    finalize_frame(slot, now, id);
    uint16_t stride = s.media_stride ? s.media_stride : 1;
    start_frame(s, id);
    // This packet belongs to the new frame (id is data, not a marker), so
    // shift the synthetic start back by one stride to keep the loss math
    // balanced with the type=03 case.
    s.frame_start_id = (uint16_t)(id - stride);
    s.recovered++;
    if (s.recovered <= 3 || s.recovered % 50 == 0) {
      xlog_printf("CAM%d SOI BOUNDARY (recovered=%u)\n", slot,
                  (unsigned)s.recovered);
    }
  }
#endif

  s.frame_rx++;
  size_t chunk = n - 8;
  if (s.assemble_len + chunk <= MAX_JPEG_SIZE) {
    memcpy(s.assemble + s.assemble_len, buf + 8, chunk);
    s.assemble_len += chunk;
  } else {
    s.assemble_len = 0;
  }
}

static void process_drw(int slot, uint8_t* buf, int n, uint32_t now) {
  CameraSession& s = sessions[slot];

  uint16_t id = (buf[6] << 8) | buf[7];
  uint8_t stream = buf[5];
  uint16_t dcmd = (buf[10] << 8) | buf[11];

  uint8_t ack[32];
  int ack_len = make_drw_ack(ack, stream, id);
  send_to_session(s, ack, ack_len);

  if (stream == 1) {
    process_media(slot, buf, n, now, id);
    return;
  }

  if (dcmd == 0x2011 && !s.got_ticket) {
    int payload_len = (buf[12] | (buf[13] << 8));
    int dec_len = payload_len - 4;
    if (dec_len > 0 && 20 + dec_len <= n) {
      xq_dec(buf + 20, dec_len, 4);
    }
    memcpy(s.ticket, buf + 24, 4);

    camera_lock();
    s.got_ticket = true;
    s.ticket_ms = now;
    s.last_rx = now;
    camera_unlock();

    xlog_printf("CAM%d TICKET %02x%02x%02x%02x\n", slot, s.ticket[0],
               s.ticket[1], s.ticket[2], s.ticket[3]);
    send_vps_sv(slot);
  }
}

static void process_packet(int slot, uint8_t* buf, int n, uint32_t now) {
  CameraSession& s = sessions[slot];
  if (n < 4) return;

  if (buf[0] == 0xf1 && buf[1] == 0x41 && n >= 24) {
    memcpy(s.punch, buf, 24);
    camera_name_from_punch(s.punch, s.name, sizeof(s.name));

    camera_lock();
    s.got_punch = true;
    s.got_ticket = false;
    s.last_rx = now;
    camera_unlock();

    s.assemble_len = 0;
    xlog_printf("CAM%d PUNCH %s\n", slot, s.name);
    send_p2prdy_and_login(slot);
    s.last_action = now;
    return;
  }

  if (buf[0] == 0xf1 && buf[1] == 0xe0) {
    uint8_t alive[4] = {0xf1, 0xe1, 0x00, 0x00};
    send_to_session(s, alive, sizeof(alive));
    return;
  }

  if (buf[0] == 0xf1 && buf[1] == 0xd0 && n >= 20) {
    process_drw(slot, buf, n, now);
  }
}

static void restart_session(int slot, uint32_t now, const char* reason) {
  CameraSession& s = sessions[slot];
  xlog_printf("CAM%d %s, RESTARTING\n", slot, reason);
  s.got_punch = false;
  s.got_ticket = false;
  s.frame_open = false;
  s.frame_rx = 0;
  s.media_stride = 0;
  s.assemble_len = 0;
  s.last_action = now;
  send_lan_search(slot);
}

static void camera_task(void* arg) {
  (void)arg;
  static uint8_t rxbuf[2048];

  for (;;) {
    struct pollfd fds[MAX_CAMERAS];
    int slots[MAX_CAMERAS];
    int nfds = 0;

    for (int i = 0; i < MAX_CAMERAS; i++) {
      if (sessions[i].active && sessions[i].fd >= 0) {
        fds[nfds].fd = sessions[i].fd;
        fds[nfds].events = POLLIN;
        fds[nfds].revents = 0;
        slots[nfds] = i;
        nfds++;
      }
    }

    int rc = poll(fds, nfds, 10);
    if (rc > 0) {
      for (int k = 0; k < nfds; k++) {
        if (!(fds[k].revents & POLLIN)) continue;
        int slot = slots[k];

        for (int drain = 0; drain < 32; drain++) {
          struct sockaddr_in from;
          socklen_t fromlen = sizeof(from);
          int n = recvfrom(fds[k].fd, rxbuf, sizeof(rxbuf), 0,
                           (struct sockaddr*)&from, &fromlen);
          if (n <= 0) break;
          uint32_t now = millis();
          sessions[slot].last_rx = now;
          process_packet(slot, rxbuf, n, now);
        }
      }
    }

    uint32_t now = millis();
    for (int i = 0; i < MAX_CAMERAS; i++) {
      CameraSession& s = sessions[i];
      if (!s.active) continue;

      if (!s.got_punch) {
        if (now - s.last_action >= CAMERA_RETRY_MS) {
          send_lan_search(i);
          s.last_action = now;
        }
      } else if (!s.got_ticket) {
        if (now - s.last_action >= CAMERA_LOGIN_RETRY_MS) {
          send_p2prdy_and_login(i);
          s.last_action = now;
        }
      } else if (now - s.last_rx > CAMERA_RX_TIMEOUT_MS) {
        restart_session(i, now, "TIMEOUT");
      } else if (s.frames == 0
                     ? (now - s.ticket_ms > CAMERA_MEDIA_STALL_MS)
                     : (now - s.last_frame_ms > CAMERA_MEDIA_STALL_MS)) {
        restart_session(i, now, "MEDIA STALL");
      }
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

static uint8_t* alloc_frame_buffer(size_t size) {
#ifdef BOARD_HAS_PSRAM
  uint8_t* p = (uint8_t*)heap_caps_malloc(
      size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (p) {
    xlog_println("CAM buffer allocated in PSRAM");
    return p;
  }
  xlog_println("CAM PSRAM alloc failed, falling back to internal");
#endif
  return (uint8_t*)heap_caps_malloc(
      size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static bool start_session(int slot) {
  if (slot < 0 || slot >= MAX_CAMERAS) return false;
  if (sessions[slot].active) return true;
  if (!eeprom.cameras[slot].enabled) return false;
  if (eeprom.cameras[slot].ip[0] == '\0') {
    xlog_printf("CAM%d NO IP\n", slot);
    return false;
  }

  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    xlog_printf("CAM%d SOCKET FAIL\n", slot);
    return false;
  }

  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);

  int rcvbuf = CAMERA_SOCKET_RX_BUFFER;
  if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) < 0) {
    xlog_printf("CAM%d SO_RCVBUF not available\n", slot);
  }

  struct sockaddr_in local;
  memset(&local, 0, sizeof(local));
  local.sin_family = AF_INET;
  local.sin_addr.s_addr = htonl(INADDR_ANY);
  local.sin_port = 0;
  if (bind(fd, (struct sockaddr*)&local, sizeof(local)) < 0) {
    xlog_printf("CAM%d BIND FAIL\n", slot);
    close(fd);
    return false;
  }

  uint8_t* assemble = alloc_frame_buffer(MAX_JPEG_SIZE);
  uint8_t* last_jpeg = alloc_frame_buffer(MAX_JPEG_SIZE);
  if (!assemble || !last_jpeg) {
    xlog_printf("CAM%d BUFFER FAIL\n", slot);
    if (assemble) free(assemble);
    if (last_jpeg) free(last_jpeg);
    close(fd);
    return false;
  }

  CameraSession& s = sessions[slot];
  memset(&s, 0, sizeof(s));
  s.fd = fd;
  s.slot = slot;
  s.active = true;
  s.assemble = assemble;
  s.last_jpeg = last_jpeg;
  s.last_rx = millis();
  s.last_action = millis();
  s.fps_window_start = millis();
  s.addr.sin_family = AF_INET;
  s.addr.sin_port = htons(CAMERA_UDP_PORT);
  inet_pton(AF_INET, eeprom.cameras[slot].ip, &s.addr.sin_addr);
  strlcpy(s.name, eeprom.cameras[slot].name, sizeof(s.name));

  xlog_printf("CAM%d START %s:%d heap=%u psram=%u\n", slot,
              eeprom.cameras[slot].ip, CAMERA_UDP_PORT,
              (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
  send_lan_search(slot);
  return true;
}

void init_camera() {
  g_mutex = xSemaphoreCreateMutex();
  memset(sessions, 0, sizeof(sessions));
  for (int i = 0; i < MAX_CAMERAS; i++) sessions[i].fd = -1;

  int started = 0;
  for (int i = 0; i < MAX_CAMERAS; i++) {
    if (!eeprom.cameras[i].enabled) continue;
    if (started >= MAX_ACTIVE_CAMERAS) {
      xlog_printf("CAM%d SKIPPED (max active %d)\n", i, MAX_ACTIVE_CAMERAS);
      continue;
    }
    if (start_session(i)) started++;
  }

  xlog_printf("CAM TASK CORE 0 (chip cores=%u)\n", ESP.getChipCores());
  xTaskCreatePinnedToCore(camera_task, "camera", 8192, NULL, 5, NULL, 0);
}

void camera_lock() {
  if (g_mutex) xSemaphoreTake(g_mutex, portMAX_DELAY);
}

void camera_unlock() {
  if (g_mutex) xSemaphoreGive(g_mutex);
}

bool camera_frame_view(int slot, const uint8_t** ptr, size_t* len,
                       uint32_t* seq) {
  if (slot < 0 || slot >= MAX_CAMERAS) return false;
  CameraSession& s = sessions[slot];
  if (!s.active || s.last_jpeg_len == 0) return false;
  *ptr = s.last_jpeg;
  *len = s.last_jpeg_len;
  *seq = s.frame_seq;
  return true;
}

uint32_t camera_frame_seq(int slot) {
  if (slot < 0 || slot >= MAX_CAMERAS) return 0;
  camera_lock();
  uint32_t seq = sessions[slot].active ? sessions[slot].frame_seq : 0;
  camera_unlock();
  return seq;
}

bool camera_get_status(int slot, CameraStatus* out) {
  if (slot < 0 || slot >= MAX_CAMERAS || !out) return false;
  memset(out, 0, sizeof(*out));
  out->configured = eeprom.cameras[slot].enabled;
  out->port = eeprom.cameras[slot].port;
  out->clients = g_stream_clients[slot];
  strlcpy(out->name, eeprom.cameras[slot].name, sizeof(out->name));
  strlcpy(out->ip, eeprom.cameras[slot].ip, sizeof(out->ip));

  camera_lock();
  CameraSession& s = sessions[slot];
  if (s.active) {
    if (out->name[0] == '\0') strlcpy(out->name, s.name, sizeof(out->name));
    out->got_ticket = s.got_ticket;
    out->online = s.got_ticket && (millis() - s.last_rx < CAMERA_RX_TIMEOUT_MS);
    out->frames = s.frames;
    out->dropped = s.dropped;
    out->recovered = s.recovered;
    out->fps = s.fps;
    out->last_frame_ms = s.last_frame_ms ? (millis() - s.last_frame_ms) : 0;
  }
  camera_unlock();
  return true;
}

void camera_set_clients(int slot, int n) {
  if (slot >= 0 && slot < MAX_CAMERAS) g_stream_clients[slot] = n;
}

void camera_name_from_punch(const uint8_t* punch, char* out, size_t out_size) {
  String s;
  String run;
  for (int i = 0; i < 24; i++) {
    char c = (char)punch[i];
    if (c >= 32 && c <= 126) {
      run += c;
    } else {
      if (run.length() >= 3) {
        if (s.length()) s += "-";
        s += run;
      }
      run = "";
    }
  }
  if (run.length() >= 3) {
    if (s.length()) s += "-";
    s += run;
  }
  if (s.length() == 0) s = "camera";
  strlcpy(out, s.c_str(), out_size);
}

int discover_scan(DiscoveredCamera* out, int max) {
  int count = 0;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) return 0;

  int yes = 1;
  setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);

  struct sockaddr_in local;
  memset(&local, 0, sizeof(local));
  local.sin_family = AF_INET;
  local.sin_addr.s_addr = htonl(INADDR_ANY);
  local.sin_port = 0;
  if (bind(fd, (struct sockaddr*)&local, sizeof(local)) < 0) {
    close(fd);
    return 0;
  }

  uint8_t lan[4] = {0xf1, 0x30, 0x00, 0x00};
  struct sockaddr_in bcast;
  memset(&bcast, 0, sizeof(bcast));
  bcast.sin_family = AF_INET;
  bcast.sin_port = htons(CAMERA_UDP_PORT);
  bcast.sin_addr.s_addr = htonl(INADDR_BROADCAST);
  sendto(fd, lan, sizeof(lan), 0, (struct sockaddr*)&bcast, sizeof(bcast));

  IPAddress ip = WiFi.localIP();
  IPAddress mask = WiFi.subnetMask();
  IPAddress subnet;
  for (int i = 0; i < 4; i++) {
    subnet[i] = (ip[i] & mask[i]) | (~mask[i] & 0xff);
  }
  char subnet_ip[16];
  snprintf(subnet_ip, sizeof(subnet_ip), "%u.%u.%u.%u", subnet[0], subnet[1],
           subnet[2], subnet[3]);
  inet_aton(subnet_ip, &bcast.sin_addr);
  sendto(fd, lan, sizeof(lan), 0, (struct sockaddr*)&bcast, sizeof(bcast));

  uint32_t start = millis();
  uint8_t buf[2048];
  while (millis() - start < 2000) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int rc = poll(&pfd, 1, 100);
    if (rc <= 0) continue;

    for (int drain = 0; drain < 16; drain++) {
      struct sockaddr_in from;
      socklen_t fromlen = sizeof(from);
      int n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr*)&from,
                       &fromlen);
      if (n <= 0) break;
      if (n < 24 || buf[0] != 0xf1 || buf[1] != 0x41) continue;

      char ipstr[16];
      inet_ntop(AF_INET, &from.sin_addr, ipstr, sizeof(ipstr));

      bool dup = false;
      for (int i = 0; i < count; i++) {
        if (strcmp(out[i].ip, ipstr) == 0) {
          dup = true;
          break;
        }
      }
      if (dup || count >= max) continue;

      memset(&out[count], 0, sizeof(out[count]));
      memcpy(out[count].punch, buf, 24);
      strlcpy(out[count].ip, ipstr, sizeof(out[count].ip));
      camera_name_from_punch(out[count].punch, out[count].name,
                             sizeof(out[count].name));
      count++;
    }
  }

  close(fd);
  return count;
}
