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

#define MAX_CLIENTS_TOTAL (MAX_CAMERAS * MAX_CLIENTS_PER_STREAM)

struct StreamClient {
  int fd;
  int slot;
  bool streaming;
  uint32_t last_seq;
  char req[MAX_HTTP_REQUEST];
  int req_len;
};

static int listen_fds[MAX_CAMERAS];
static int slot_clients[MAX_CAMERAS];
static StreamClient clients[MAX_CLIENTS_TOTAL];

static void send_all_close_on_fail(int fd, const void* data, size_t len,
                                   bool* ok) {
  if (!*ok) return;
  const uint8_t* p = (const uint8_t*)data;
  while (len > 0) {
    ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
    if (n <= 0) {
      *ok = false;
      return;
    }
    p += n;
    len -= (size_t)n;
  }
}

static void close_client(StreamClient& c) {
  if (c.fd >= 0) {
    close(c.fd);
    c.fd = -1;
  }
  if (c.streaming && c.slot >= 0 && c.slot < MAX_CAMERAS) {
    if (slot_clients[c.slot] > 0) slot_clients[c.slot]--;
    camera_set_clients(c.slot, slot_clients[c.slot]);
  }
  c.streaming = false;
  c.req_len = 0;
  c.slot = -1;
}

static void client_send_status(StreamClient& c, int code, const char* reason,
                               const char* body) {
  char hdr[256];
  int hl = snprintf(hdr, sizeof(hdr),
                    "HTTP/1.0 %d %s\r\nContent-Type: text/html\r\n"
                    "Connection: close\r\n\r\n",
                    code, reason);
  bool ok = true;
  send_all_close_on_fail(c.fd, hdr, (size_t)hl, &ok);
  if (body) send_all_close_on_fail(c.fd, body, strlen(body), &ok);
  close_client(c);
}

static void handle_request(StreamClient& c) {
  if (strstr(c.req, "GET /video") != nullptr) {
    if (slot_clients[c.slot] >= MAX_CLIENTS_PER_STREAM) {
      client_send_status(c, 503, "Service Unavailable", "too many clients");
      return;
    }

    const char* hdr =
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=x5frame\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: close\r\n\r\n";
    bool ok = true;
    send_all_close_on_fail(c.fd, hdr, strlen(hdr), &ok);
    if (!ok) {
      close_client(c);
      return;
    }
    c.streaming = true;
    c.last_seq = 0xFFFFFFFFu;
    slot_clients[c.slot]++;
    camera_set_clients(c.slot, slot_clients[c.slot]);
    xlog_printf("STREAM CAM%d client connected (total %d)\n", c.slot,
               slot_clients[c.slot]);
    return;
  }

  if (strstr(c.req, "GET /snapshot") != nullptr) {
    const uint8_t* ptr = nullptr;
    size_t len = 0;
    uint32_t seq = 0;
    camera_lock();
    bool has = camera_frame_view(c.slot, &ptr, &len, &seq);
    if (!has) {
      camera_unlock();
      client_send_status(c, 503, "Service Unavailable", "no frame yet");
      return;
    }

    char hdr[192];
    int hl = snprintf(hdr, sizeof(hdr),
                      "HTTP/1.0 200 OK\r\nContent-Type: image/jpeg\r\n"
                      "Content-Length: %u\r\nConnection: close\r\n\r\n",
                      (unsigned)len);
    bool ok = true;
    send_all_close_on_fail(c.fd, hdr, (size_t)hl, &ok);
    send_all_close_on_fail(c.fd, ptr, len, &ok);
    camera_unlock();
    close_client(c);
    return;
  }

  client_send_status(c, 200, "OK",
                     "<html><body><h3>X5 stream</h3>"
                     "<p><a href='/video'>/video</a> | "
                     "<a href='/snapshot'>/snapshot</a></p></body></html>");
}

static void handle_client_readable(StreamClient& c) {
  if (c.streaming) {
    char tmp[64];
    int n = recv(c.fd, tmp, sizeof(tmp), MSG_DONTWAIT);
    if (n == 0) {
      close_client(c);
    } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
      close_client(c);
    }
    return;
  }

  int room = (int)sizeof(c.req) - 1 - c.req_len;
  if (room <= 0) {
    close_client(c);
    return;
  }

  int n = recv(c.fd, c.req + c.req_len, room, MSG_DONTWAIT);
  if (n == 0) {
    close_client(c);
    return;
  }
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) return;
    close_client(c);
    return;
  }
  c.req_len += n;
  c.req[c.req_len] = 0;

  char* end = strstr(c.req, "\r\n");
  if (!end) end = strchr(c.req, '\n');
  if (!end) return;

  *end = 0;
  handle_request(c);
}

static void accept_client(int slot) {
  while (true) {
    struct sockaddr_in cli;
    socklen_t clen = sizeof(cli);
    int fd = accept(listen_fds[slot], (struct sockaddr*)&cli, &clen);
    if (fd < 0) return;

    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    int free_idx = -1;
    for (int i = 0; i < MAX_CLIENTS_TOTAL; i++) {
      if (clients[i].fd < 0) {
        free_idx = i;
        break;
      }
    }
    if (free_idx < 0) {
      close(fd);
      continue;
    }

    StreamClient& c = clients[free_idx];
    memset(&c, 0, sizeof(c));
    c.fd = fd;
    c.slot = slot;
    c.req_len = 0;
    xlog_printf("STREAM CAM%d accept fd=%d\n", slot, fd);
  }
}

static void send_frames() {
  for (int slot = 0; slot < MAX_CAMERAS; slot++) {
    bool has_stream = false;
    for (int i = 0; i < MAX_CLIENTS_TOTAL; i++) {
      if (clients[i].fd >= 0 && clients[i].streaming &&
          clients[i].slot == slot) {
        has_stream = true;
        break;
      }
    }
    if (!has_stream) continue;

    camera_lock();
    const uint8_t* ptr = nullptr;
    size_t len = 0;
    uint32_t seq = 0;
    bool has_frame = camera_frame_view(slot, &ptr, &len, &seq);

    if (has_frame) {
      for (int i = 0; i < MAX_CLIENTS_TOTAL; i++) {
        StreamClient& c = clients[i];
        if (c.fd < 0 || !c.streaming || c.slot != slot) continue;
        if (c.last_seq == seq) continue;

        char hdr[128];
        int hl = snprintf(hdr, sizeof(hdr),
                          "--x5frame\r\nContent-Type: image/jpeg\r\n"
                          "Content-Length: %u\r\n\r\n",
                          (unsigned)len);
        bool ok = true;
        send_all_close_on_fail(c.fd, hdr, (size_t)hl, &ok);
        send_all_close_on_fail(c.fd, ptr, len, &ok);
        send_all_close_on_fail(c.fd, "\r\n", 2, &ok);
        if (ok) {
          c.last_seq = seq;
        } else {
          close_client(c);
        }
      }
    }
    camera_unlock();
  }
}

static void stream_task(void* arg) {
  (void)arg;

  for (;;) {
    struct pollfd fds[MAX_CAMERAS + MAX_CLIENTS_TOTAL];
    int types[MAX_CAMERAS + MAX_CLIENTS_TOTAL];
    int idxs[MAX_CAMERAS + MAX_CLIENTS_TOTAL];
    int nfds = 0;

    for (int i = 0; i < MAX_CAMERAS; i++) {
      if (listen_fds[i] < 0) continue;
      fds[nfds].fd = listen_fds[i];
      fds[nfds].events = POLLIN;
      fds[nfds].revents = 0;
      types[nfds] = 0;
      idxs[nfds] = i;
      nfds++;
    }

    for (int i = 0; i < MAX_CLIENTS_TOTAL; i++) {
      if (clients[i].fd < 0) continue;
      fds[nfds].fd = clients[i].fd;
      fds[nfds].events = POLLIN;
      fds[nfds].revents = 0;
      types[nfds] = 1;
      idxs[nfds] = i;
      nfds++;
    }

    int rc = poll(fds, nfds, 10);
    if (rc > 0) {
      for (int k = 0; k < nfds; k++) {
        if (!(fds[k].revents & (POLLIN | POLLHUP | POLLERR))) continue;
        if (types[k] == 0) {
          accept_client(idxs[k]);
        } else {
          StreamClient& c = clients[idxs[k]];
          if (c.fd >= 0) handle_client_readable(c);
        }
      }
    }

    send_frames();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void init_stream() {
  memset(clients, 0, sizeof(clients));
  for (int i = 0; i < MAX_CLIENTS_TOTAL; i++) clients[i].fd = -1;
  for (int i = 0; i < MAX_CAMERAS; i++) {
    listen_fds[i] = -1;
    slot_clients[i] = 0;
    camera_set_clients(i, 0);
  }

  for (int i = 0; i < MAX_CAMERAS; i++) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
      xlog_printf("STREAM CAM%d socket fail\n", i);
      continue;
    }

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(CAMERA_BASE_PORT + i);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
      xlog_printf("STREAM CAM%d bind %d fail\n", i, CAMERA_BASE_PORT + i);
      close(fd);
      continue;
    }

    if (listen(fd, 4) < 0) {
      xlog_printf("STREAM CAM%d listen fail\n", i);
      close(fd);
      continue;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    listen_fds[i] = fd;
    xlog_printf("STREAM CAM%d listening on %d\n", i, CAMERA_BASE_PORT + i);
  }

  xTaskCreate(stream_task, "stream", 8192, NULL, 4, NULL);
}
