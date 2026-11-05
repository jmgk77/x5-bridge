// Direct port of impl.ts (makeDataReadWrite, SendVideoResolution, SendStartVideo)
// and handlers.ts (makeDrwAck). Replicates the flow that works in cam-reverse.
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

static const char LOGIN_HEX[] =
  "f1d000b0d1000000110a2010a400ff00000000006f01010101010101010101010101010101010101010101010101010160656c686f01010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010160656c68";

static int hex2bin(const char* h, uint8_t* o, int m) {
    int n = strlen(h); if (n % 2) return -1;
    int b = n / 2; if (b > m) return -1;
    for (int i = 0; i < b; i++) { unsigned int v; if (sscanf(h+2*i, "%2x", &v) != 1) return -1; o[i] = v; }
    return b;
}

// Exact XqBytesEnc from func_replacements.js
static void xq_enc(uint8_t* buf, int len, int rot) {
    uint8_t* nb = (uint8_t*)malloc(len);
    for (int i = 0; i < len; i++) nb[i] = (buf[i] & 1) ? (buf[i]-1) : (buf[i]+1);
    for (int i = 0; i < len-rot; i++) buf[i] = nb[i+rot];
    for (int i = 0; i < rot; i++) buf[len-rot+i] = nb[i];
    free(nb);
}

// Exact XqBytesDec from func_replacements.js
static void xq_dec(uint8_t* buf, int len, int rot) {
    uint8_t* nb = (uint8_t*)malloc(len);
    for (int i = 0; i < len; i++) nb[i] = (buf[i] & 1) ? (buf[i]-1) : (buf[i]+1);
    for (int i = rot; i < len; i++) buf[i] = nb[i-rot];
    for (int i = 0; i < rot; i++) buf[i] = nb[len-rot+i];
    free(nb);
}

static uint16_t u16_swap(uint16_t x) { return (x >> 8) | (x << 8); }

// Port of makeDataReadWrite (impl.ts lines 7-22)
// Returns the total packet size.
static int make_data_read_write(uint8_t* out, uint16_t cmd, uint16_t id,
                                 const uint8_t* ticket,
                                 const uint8_t* data, int data_len) {
    int pkt_len = 0x10 + 0x4; // 20
    int payload_len = 0x4;    // 4

    if (data && data_len > 4) {
        pkt_len += data_len;
        payload_len += data_len;
    }

    out[0] = 0xf1; out[1] = 0xd0; // Commands.Drw
    uint16_t l = pkt_len - 4;
    out[2] = (l >> 8) & 0xff; out[3] = l & 0xff;
    out[4] = 0xd1;
    out[5] = 0x00; // CHANNEL
    out[6] = (id >> 8) & 0xff; out[7] = id & 0xff; // outgoingCommandId
    out[8] = 0x11; out[9] = 0x0a; // START_CMD = 0x110a
    out[10] = (cmd >> 8) & 0xff; out[11] = cmd & 0xff;
    uint16_t pl = u16_swap(payload_len);
    out[12] = (pl >> 8) & 0xff; out[13] = pl & 0xff;
    out[14] = 0x00; out[15] = 0x00; // ccDest (StartVideo=0x0000, VideoParamSet=undefined→0)
    memcpy(out + 16, ticket, 4);
    if (data && data_len > 4) {
        memcpy(out + 20, data, data_len);
    }
    return pkt_len;
}

// Port of makeDrwAck (handlers.ts lines 377-407)
// Returns the ACK size.
static int make_drw_ack(uint8_t* out, uint8_t stream, uint16_t pkt_id) {
    uint16_t reply_len = 1 * 2 + 4; // item_count*2 + 4
    memset(out, 0, 32);
    out[0] = 0xf1; out[1] = 0xd1; // Commands.DrwAck
    out[2] = (reply_len >> 8) & 0xff; out[3] = reply_len & 0xff;
    out[4] = 0xd2;
    out[5] = stream;
    out[6] = 0x00; out[7] = 0x01; // item_count = 1
    out[8] = (pkt_id >> 8) & 0xff; out[9] = pkt_id & 0xff;
    return 32;
}

int main(int argc, char** argv) {
    const char* ip = argc > 1 ? argv[1] : "192.168.0.10";
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in cam = {};
    cam.sin_family = AF_INET;
    cam.sin_port = htons(32108);
    inet_pton(AF_INET, ip, &cam.sin_addr);

    uint8_t buf[8192];
    socklen_t fromlen;
    struct sockaddr_in from;
    struct timeval tv;

    // ===== 1. LanSearch =====
    uint8_t lan[4] = { 0xf1, 0x30, 0x00, 0x00 };
    sendto(sock, lan, 4, 0, (struct sockaddr*)&cam, sizeof(cam));
    tv.tv_sec = 1; tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    fromlen = sizeof(from);
    int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&from, &fromlen);
    if (n < 24 || buf[0] != 0xf1 || buf[1] != 0x41) { printf("!! no PunchPkt\n"); return 1; }
    printf("[1] LanSearch ok\n");

    // ===== 2. P2pRdy (echo) =====
    uint8_t p2prdy[24]; memcpy(p2prdy, buf, 24); p2prdy[1] = 0x42;
    sendto(sock, p2prdy, 24, 0, (struct sockaddr*)&cam, sizeof(cam));
    printf("[2] P2pRdy\n");

    // ===== 3. Login =====
    uint8_t login[512];
    int login_len = hex2bin(LOGIN_HEX, login, sizeof(login));
    sendto(sock, login, login_len, 0, (struct sockaddr*)&cam, sizeof(cam));
    printf("[3] Login %dB\n", login_len);

    // ===== 4. Loop =====
    uint8_t ticket[4] = { 0, 0, 0, 0 };
    bool got_ticket = false;
    int frame_count = 0;
    int jpeg_count = 0;
    #define MAX_FRAME (64*1024)
    static uint8_t cur_frame[MAX_FRAME];
    int cur_frame_len = 0;
    time_t start = time(NULL);
    tv.tv_sec = 1; tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (time(NULL) - start < 30) {
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

            // Always ACK (same as makeDrwAck)
            uint8_t ack[32];
            int ack_len = make_drw_ack(ack, stream, id);
            sendto(sock, ack, ack_len, 0, (struct sockaddr*)&cam, sizeof(cam));

            if (stream == 1) {
                bool is_framed = (n >= 13 &&
                    buf[8]==0x55 && buf[9]==0xaa && buf[10]==0x15 && buf[11]==0xa8);
                uint8_t stype = is_framed ? buf[12] : 0;

                if (is_framed && stype == 0x03) {
                    // start of a new JPEG: close the previous one
                    if (cur_frame_len > 100) {
                        int end_i = -1;
                        for (int i = 0; i < cur_frame_len-1; i++)
                            if (cur_frame[i]==0xff && cur_frame[i+1]==0xd9) end_i = i;
                        if (end_i > 0 && cur_frame[0]==0xff && cur_frame[1]==0xd8) {
                            char fn[64];
                            sprintf(fn, "frame%d.jpg", jpeg_count);
                            FILE* f = fopen(fn, "wb");
                            if (f) { fwrite(cur_frame, 1, end_i+2, f); fclose(f); }
                            printf("    [JPEG %d] %s %d bytes\n", jpeg_count, fn, end_i+2);
                            jpeg_count++;
                        }
                    }
                    cur_frame_len = 0;
                    // add nothing: the SOI comes in the next packet
                }
                else if (is_framed && stype == 0x06) {
                    // audio, ignore
                }
                else if (!is_framed && cur_frame_len >= 0 && n > 8) {
                    if (cur_frame_len + (n-8) < MAX_FRAME) {
                        memcpy(cur_frame + cur_frame_len, buf + 8, n - 8);
                        cur_frame_len += (n - 8);
                    }
                }
                continue;
            }

            // stream == 0, control
            if (dcmd == 0x2011 && !got_ticket) {
                // ConnectUserAck: decrypt payload (offset 20, len-4), ticket @ 0x18
                int payload_len = (buf[12] | (buf[13] << 8)); // readU16LE
                int dec_len = payload_len - 4;
                if (dec_len > 0 && 20 + dec_len <= n) {
                    xq_dec(buf + 20, dec_len, 4);
                }
                memcpy(ticket, buf + 24, 4);
                got_ticket = true;
                printf("    << ConnectUserAck (id=%u), ticket=%02x%02x%02x%02x\n",
                       id, ticket[0], ticket[1], ticket[2], ticket[3]);

                // === startVideoStream: VPS + SV together (same as TS) ===
                // VPS: 640x480 payload = [01,00,00,00,02,00,00,00], encrypted
                uint8_t vp_payload[8] = { 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00 };
                xq_enc(vp_payload, 8, 4);
                uint8_t vps[64];
                int vps_len = make_data_read_write(vps, 0x1830, 1, ticket, vp_payload, 8);
                sendto(sock, vps, vps_len, 0, (struct sockaddr*)&cam, sizeof(cam));
                printf("    >> VideoParamSet id=1 (%dB)\n", vps_len);

                // SV: no payload
                uint8_t sv[64];
                int sv_len = make_data_read_write(sv, 0x1030, 2, ticket, NULL, 0);
                sendto(sock, sv, sv_len, 0, (struct sockaddr*)&cam, sizeof(cam));
                printf("    >> StartVideo id=2 (%dB)\n", sv_len);
            }
            else {
                printf("    << Drw id=%u stream=%u cmd=%04x len=%dB -> ACK\n",
                       id, stream, dcmd, n);
            }
        }
    }

    printf("\n=== got_ticket=%d jpegs=%d (pacotes=%d) ===\n", got_ticket, jpeg_count, frame_count);
    close(sock);
    return 0;
}
