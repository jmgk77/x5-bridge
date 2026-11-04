#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <csignal>
#include <thread>
#include <mutex>
#include <vector>

static std::vector<int> g_clients;
static std::mutex g_clients_mu;
static uint8_t g_last_frame[65536];
static int g_last_frame_len = 0;
static std::mutex g_last_frame_mu;

static const char LOGIN_HEX[] =
  "f1d000b0d1000000110a2010a400ff00000000006f01010101010101010101010101010101010101010101010101010160656c686f01010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010160656c68";

static int hex2bin(const char* h, uint8_t* o, int m) {
    int n = strlen(h); if (n % 2) return -1;
    int b = n / 2; if (b > m) return -1;
    for (int i = 0; i < b; i++) { unsigned int v; if (sscanf(h+2*i, "%2x", &v) != 1) return -1; o[i] = v; }
    return b;
}
static void xq_enc(uint8_t* buf, int len, int rot) {
    uint8_t* nb = (uint8_t*)malloc(len);
    for (int i = 0; i < len; i++) nb[i] = (buf[i] & 1) ? (buf[i]-1) : (buf[i]+1);
    for (int i = 0; i < len-rot; i++) buf[i] = nb[i+rot];
    for (int i = 0; i < rot; i++) buf[len-rot+i] = nb[i];
    free(nb);
}
static void xq_dec(uint8_t* buf, int len, int rot) {
    uint8_t* nb = (uint8_t*)malloc(len);
    for (int i = 0; i < len; i++) nb[i] = (buf[i] & 1) ? (buf[i]-1) : (buf[i]+1);
    for (int i = rot; i < len; i++) buf[i] = nb[i-rot];
    for (int i = 0; i < rot; i++) buf[i] = nb[len-rot+i];
    free(nb);
}
static uint16_t u16_swap(uint16_t x) { return (x >> 8) | (x << 8); }

static int make_data_read_write(uint8_t* out, uint16_t cmd, uint16_t id,
                                 const uint8_t* ticket, const uint8_t* data, int data_len) {
    int pkt_len = 0x10 + 0x4;
    int payload_len = 0x4;
    if (data && data_len > 4) { pkt_len += data_len; payload_len += data_len; }
    out[0]=0xf1; out[1]=0xd0;
    uint16_t l = pkt_len - 4; out[2]=(l>>8)&0xff; out[3]=l&0xff;
    out[4]=0xd1; out[5]=0x00;
    out[6]=(id>>8)&0xff; out[7]=id&0xff;
    out[8]=0x11; out[9]=0x0a;
    out[10]=(cmd>>8)&0xff; out[11]=cmd&0xff;
    uint16_t pl = u16_swap(payload_len); out[12]=(pl>>8)&0xff; out[13]=pl&0xff;
    out[14]=0x00; out[15]=0x00;
    memcpy(out+16, ticket, 4);
    if (data && data_len > 4) memcpy(out+20, data, data_len);
    return pkt_len;
}
static int make_drw_ack(uint8_t* out, uint8_t stream, uint16_t pkt_id) {
    memset(out, 0, 32);
    out[0]=0xf1; out[1]=0xd1; out[2]=0x00; out[3]=0x06;
    out[4]=0xd2; out[5]=stream; out[6]=0x00; out[7]=0x01;
    out[8]=(pkt_id>>8)&0xff; out[9]=pkt_id&0xff;
    return 32;
}

static void broadcast_frame(const uint8_t* jpeg, int len) {
    {
        std::lock_guard<std::mutex> lk(g_last_frame_mu);
        if (len <= (int)sizeof(g_last_frame)) {
            memcpy(g_last_frame, jpeg, len);
            g_last_frame_len = len;
        }
    }
    char hdr[128];
    int hdr_len = snprintf(hdr, sizeof(hdr),
        "--x5frame\r\nContent-Type: image/jpeg\r\nContent-Length: %d\r\n\r\n", len);
    std::lock_guard<std::mutex> lk(g_clients_mu);
    for (auto it = g_clients.begin(); it != g_clients.end(); ) {
        bool ok = send(*it, hdr, hdr_len, MSG_NOSIGNAL) == hdr_len &&
                  send(*it, jpeg, len, MSG_NOSIGNAL) == len &&
                  send(*it, "\r\n", 2, MSG_NOSIGNAL) == 2;
        if (ok) ++it;
        else { close(*it); it = g_clients.erase(it); }
    }
}

static void http_thread(int listen_fd) {
    while (true) {
        struct sockaddr_in cli; socklen_t cl = sizeof(cli);
        int cfd = accept(listen_fd, (struct sockaddr*)&cli, &cl);
        if (cfd < 0) continue;

        char req[4096]; int rn = 0;
        while (rn < (int)sizeof(req)-1) {
            int r = recv(cfd, req+rn, 1, 0);
            if (r <= 0) break;
            rn++;
            if (rn >= 4 && memcmp(req+rn-4, "\r\n\r\n", 4) == 0) break;
        }
        req[rn] = 0;

        if (strstr(req, "GET /video") != nullptr) {
            const char* hdr = "HTTP/1.0 200 OK\r\n"
                "Content-Type: multipart/x-mixed-replace; boundary=x5frame\r\n"
                "Cache-Control: no-cache\r\n"
                "Connection: close\r\n\r\n";
            send(cfd, hdr, strlen(hdr), MSG_NOSIGNAL);
            {
                std::lock_guard<std::mutex> lk(g_last_frame_mu);
                if (g_last_frame_len > 0) {
                    char fh[128];
                    int fhl = snprintf(fh, sizeof(fh),
                        "--x5frame\r\nContent-Type: image/jpeg\r\nContent-Length: %d\r\n\r\n",
                        g_last_frame_len);
                    send(cfd, fh, fhl, MSG_NOSIGNAL);
                    send(cfd, g_last_frame, g_last_frame_len, MSG_NOSIGNAL);
                    send(cfd, "\r\n", 2, MSG_NOSIGNAL);
                }
            }
            std::lock_guard<std::mutex> lk(g_clients_mu);
            g_clients.push_back(cfd);
            printf("[http] /video client connected (total %zu)\n", g_clients.size());
        }
        else if (strstr(req, "GET /snapshot") != nullptr) {
            std::lock_guard<std::mutex> lk(g_last_frame_mu);
            if (g_last_frame_len > 0) {
                char h2[256];
                int h2l = snprintf(h2, sizeof(h2),
                    "HTTP/1.0 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: %d\r\n\r\n",
                    g_last_frame_len);
                send(cfd, h2, h2l, MSG_NOSIGNAL);
                send(cfd, g_last_frame, g_last_frame_len, MSG_NOSIGNAL);
            } else {
                const char* err = "HTTP/1.0 503 Service Unavailable\r\n\r\nno frame yet";
                send(cfd, err, strlen(err), MSG_NOSIGNAL);
            }
            close(cfd);
        }
        else {
            const char* html = "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n"
                "<html><body><h1>X5 camera</h1>"
                "<img src=\"/video\" width=\"640\">"
                "<p><a href=\"/snapshot\">snapshot.jpg</a></p>"
                "</body></html>";
            send(cfd, html, strlen(html), MSG_NOSIGNAL);
            close(cfd);
        }
    }
}

int main(int argc, char** argv) {
    signal(SIGPIPE, SIG_IGN);

    const char* ip = argc > 1 ? argv[1] : "192.168.0.10";
    int port = argc > 2 ? atoi(argv[2]) : 8080;

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in haddr = {};
    haddr.sin_family = AF_INET;
    haddr.sin_port = htons(port);
    haddr.sin_addr.s_addr = INADDR_ANY;
    if (bind(listen_fd, (struct sockaddr*)&haddr, sizeof(haddr)) < 0) {
        perror("bind"); return 1;
    }
    listen(listen_fd, 10);
    std::thread t(http_thread, listen_fd);
    t.detach();
    printf("[http] listening on http://0.0.0.0:%d/video\n", port);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in cam = {};
    cam.sin_family = AF_INET;
    cam.sin_port = htons(32108);
    inet_pton(AF_INET, ip, &cam.sin_addr);

    uint8_t buf[8192];
    socklen_t fromlen;
    struct sockaddr_in from;
    struct timeval tv;

    uint8_t lan[4] = { 0xf1, 0x30, 0x00, 0x00 };
    sendto(sock, lan, 4, 0, (struct sockaddr*)&cam, sizeof(cam));
    tv.tv_sec = 1; tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    fromlen = sizeof(from);
    int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&from, &fromlen);
    if (n < 24 || buf[0] != 0xf1 || buf[1] != 0x41) { printf("!! no PunchPkt\n"); return 1; }

    uint8_t p2prdy[24]; memcpy(p2prdy, buf, 24); p2prdy[1] = 0x42;
    sendto(sock, p2prdy, 24, 0, (struct sockaddr*)&cam, sizeof(cam));

    uint8_t login[512];
    int login_len = hex2bin(LOGIN_HEX, login, sizeof(login));
    sendto(sock, login, login_len, 0, (struct sockaddr*)&cam, sizeof(cam));

    uint8_t ticket[4] = { 0, 0, 0, 0 };
    bool got_ticket = false;
    int jpeg_count = 0;
    #define MAX_FRAME (64*1024)
    static uint8_t cur_frame[MAX_FRAME];
    int cur_frame_len = 0;

    while (true) {
        n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&from, &fromlen);
        if (n < 4) continue;
        uint16_t cmd = (buf[0] << 8) | buf[1];

        if (cmd == 0xf1e0) {
            uint8_t ack[4] = { 0xf1, 0xe1, 0x00, 0x00 };
            sendto(sock, ack, 4, 0, (struct sockaddr*)&cam, sizeof(cam));
        }
        else if (cmd == 0xf1d0 && n >= 20) {
            uint16_t id = (buf[6] << 8) | buf[7];
            uint8_t stream = buf[5];
            uint16_t dcmd = (buf[10] << 8) | buf[11];

            uint8_t ack[32];
            int ack_len = make_drw_ack(ack, stream, id);
            sendto(sock, ack, ack_len, 0, (struct sockaddr*)&cam, sizeof(cam));

            if (stream == 1) {
                static int _sc = 0; if (_sc++ < 3) printf("[cam] stream=1 n=%d buf8=%02x%02x%02x%02x\n", n, buf[8],buf[9],buf[10],buf[11]);
                bool is_framed = (n >= 13 &&
                    buf[8]==0x55 && buf[9]==0xaa && buf[10]==0x15 && buf[11]==0xa8);
                uint8_t stype = is_framed ? buf[12] : 0;

                if (is_framed && stype == 0x03) {
                    if (cur_frame_len > 100) {
                        int end_i = -1;
                        for (int i = 0; i < cur_frame_len-1; i++)
                            if (cur_frame[i]==0xff && cur_frame[i+1]==0xd9) end_i = i;
                        if (end_i > 0 && cur_frame[0]==0xff && cur_frame[1]==0xd8) {
                            broadcast_frame(cur_frame, end_i+2);
                            jpeg_count++;
                            if (jpeg_count <= 3 || jpeg_count % 30 == 0) printf("[cam] JPEG %d: %d bytes (%zu clients)\n", jpeg_count, end_i+2, g_clients.size());
                        }
                    }
                    cur_frame_len = 0;
                }
                else if (is_framed && stype == 0x06) { /* audio */ }
                else if (!is_framed && n > 8) {
                    if (cur_frame_len + (n-8) < MAX_FRAME) {
                        memcpy(cur_frame + cur_frame_len, buf + 8, n - 8);
                        cur_frame_len += (n - 8);
                    }
                }
                continue;
            }

            if (dcmd == 0x2011 && !got_ticket) {
                int payload_len = (buf[12] | (buf[13] << 8));
                int dec_len = payload_len - 4;
                if (dec_len > 0 && 20 + dec_len <= n) xq_dec(buf + 20, dec_len, 4);
                memcpy(ticket, buf + 24, 4);
                got_ticket = true;
                printf("[cam] ticket=%02x%02x%02x%02x\n", ticket[0],ticket[1],ticket[2],ticket[3]);

                uint8_t vp_payload[8] = { 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00 };
                xq_enc(vp_payload, 8, 4);
                uint8_t vps[64];
                int vps_len = make_data_read_write(vps, 0x1830, 1, ticket, vp_payload, 8);
                sendto(sock, vps, vps_len, 0, (struct sockaddr*)&cam, sizeof(cam));

                uint8_t sv[64];
                int sv_len = make_data_read_write(sv, 0x1030, 2, ticket, NULL, 0);
                sendto(sock, sv, sv_len, 0, (struct sockaddr*)&cam, sizeof(cam));
            }
        }
    }

    return 0;
}
