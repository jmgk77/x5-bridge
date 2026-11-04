// Probe de bring-up: identifica chip, flash, PSRAM, heap e WiFi.
// Nao altera nada; apenas imprime um relatorio a cada 10 s.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_chip_info.h>
#include <esp_flash.h>

#ifdef CAMERA_PROBE
#include "esp_camera.h"

// Pinagem AI-Thinker ESP32-CAM
#define PWDN_GPIO_NUM   32
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM    0
#define SIOD_GPIO_NUM   26
#define SIOC_GPIO_NUM   27
#define Y9_GPIO_NUM     35
#define Y8_GPIO_NUM     34
#define Y7_GPIO_NUM     39
#define Y6_GPIO_NUM     36
#define Y5_GPIO_NUM     21
#define Y4_GPIO_NUM     19
#define Y3_GPIO_NUM     18
#define Y2_GPIO_NUM      5
#define VSYNC_GPIO_NUM  25
#define HREF_GPIO_NUM   23
#define PCLK_GPIO_NUM   22

static void camera_probe() {
    Serial.println("--- camera probe (AI-Thinker) ---");
    camera_config_t c = {};
    c.ledc_channel = LEDC_CHANNEL_0;
    c.ledc_timer = LEDC_TIMER_0;
    c.pin_pwdn = PWDN_GPIO_NUM;
    c.pin_reset = RESET_GPIO_NUM;
    c.pin_xclk = XCLK_GPIO_NUM;
    c.pin_sccb_sda = SIOD_GPIO_NUM;
    c.pin_sccb_scl = SIOC_GPIO_NUM;
    c.pin_d7 = Y9_GPIO_NUM;
    c.pin_d6 = Y8_GPIO_NUM;
    c.pin_d5 = Y7_GPIO_NUM;
    c.pin_d4 = Y6_GPIO_NUM;
    c.pin_d3 = Y5_GPIO_NUM;
    c.pin_d2 = Y4_GPIO_NUM;
    c.pin_d1 = Y3_GPIO_NUM;
    c.pin_d0 = Y2_GPIO_NUM;
    c.pin_vsync = VSYNC_GPIO_NUM;
    c.pin_href = HREF_GPIO_NUM;
    c.pin_pclk = PCLK_GPIO_NUM;
    c.xclk_freq_hz = 20000000;
    c.pixel_format = PIXFORMAT_JPEG;
    c.frame_size = FRAMESIZE_QVGA;
    c.jpeg_quality = 12;
    c.fb_count = 1;
    c.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
    c.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

    esp_err_t e = esp_camera_init(&c);
    Serial.printf("camera_init     : 0x%x (%s)\n", (unsigned)e, esp_err_to_name(e));
    if (e != ESP_OK) return;

    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        Serial.printf("sensor_pid      : 0x%02x (ver 0x%02x, midh 0x%02x, midl 0x%02x)\n",
                      (unsigned)s->id.PID, (unsigned)s->id.VER,
                      (unsigned)s->id.MIDH, (unsigned)s->id.MIDL);
        Serial.printf("sensor_status   : framesize=%d quality=%d\n",
                      (int)s->status.framesize, (int)s->status.quality);
    }
    const framesize_t sizes[] = {FRAMESIZE_QVGA, FRAMESIZE_VGA, FRAMESIZE_SVGA, FRAMESIZE_UXGA};
    const char *names[] = {"QVGA 320x240", "VGA 640x480", "SVGA 800x600", "UXGA 1600x1200"};
    for (int r = 0; r < 4; r++) {
        Serial.printf("set_framesize : %-16s ", names[r]);
        Serial.flush();
        int rc = s ? s->set_framesize(s, sizes[r]) : -1;
        Serial.printf("rc=%d\n", rc);
        if (rc != 0) continue;
        delay(1000);
        for (int i = 0; i < 3; i++) {
            camera_fb_t *fb = esp_camera_fb_get();
            if (!fb) { Serial.printf("  fb[%d]        : FAIL\n", i); break; }
            unsigned long sum = 0;
            for (unsigned k = 0; k < fb->len; k++) sum += fb->buf[k];
            Serial.printf("  fb[%d]        : OK %u bytes %ux%u fmt=%d avg=%u\n",
                          i, (unsigned)fb->len, (unsigned)fb->width, (unsigned)fb->height,
                          (int)fb->format, (unsigned)(sum / (fb->len ? fb->len : 1)));
            esp_camera_fb_return(fb);
            delay(300);
        }
    }
    esp_camera_deinit();
    Serial.println("camera_deinit   : done");
}
#endif

static int g_report = 0;

static void memline(const char *tag) {
    Serial.printf("[MEM %-22s] heap=%u free=%u min=%u maxalloc=%u lar8=%u larInt=%u psram=%u\n",
                  tag,
                  (unsigned)ESP.getHeapSize(),
                  (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMinFreeHeap(),
                  (unsigned)ESP.getMaxAllocHeap(),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL),
                  (unsigned)ESP.getFreePsram());
}

static void alloc_probe(size_t sz, int maxn) {
    void *p[8];
    int n = 0;
    for (int i = 0; i < maxn && i < 8; i++) {
        void *q = heap_caps_malloc(sz, MALLOC_CAP_8BIT);
        if (!q) break;
        p[n++] = q;
    }
    Serial.printf("  alloc %5u B x %d -> %s\n", (unsigned)sz, n, n ? "OK" : "FAIL");
    for (int i = 0; i < n; i++) free(p[i]);
}

static void full_report() {
    Serial.println();
    Serial.println("========== X5 PROBE REPORT ==========");
    esp_chip_info_t ci;
    esp_chip_info(&ci);
    Serial.printf("chip_model      : %s\n", ESP.getChipModel());
    Serial.printf("chip_revision   : v%u.%u\n", ESP.getChipRevision() / 100, ESP.getChipRevision() % 100);
    Serial.printf("chip_revision_r : %u\n", (unsigned)ESP.getChipRevision());
    Serial.printf("chip_cores      : %u\n", (unsigned)ESP.getChipCores());
    Serial.printf("cpu_freq_mhz    : %u\n", (unsigned)ESP.getCpuFreqMHz());
    Serial.printf("chip_features   : 0x%08x (wifi=%d ble=%d bt=%d embflash=%d 802154=%d)\n",
                  (unsigned)ci.features,
                  !!(ci.features & CHIP_FEATURE_WIFI_BGN),
                  !!(ci.features & CHIP_FEATURE_BLE),
                  !!(ci.features & CHIP_FEATURE_BT),
                  !!(ci.features & CHIP_FEATURE_EMB_FLASH),
                  !!(ci.features & CHIP_FEATURE_IEEE802154));
    Serial.printf("sdk_version     : %s\n", ESP.getSdkVersion());
    Serial.printf("reset_reason    : %d\n", (int)esp_reset_reason());
    Serial.printf("mac             : %s\n", WiFi.macAddress().c_str());
    Serial.printf("flash_size      : %u B (%u MB)\n", (unsigned)ESP.getFlashChipSize(), (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)));
    Serial.printf("flash_speed     : %u Hz\n", (unsigned)ESP.getFlashChipSpeed());
    Serial.printf("flash_mode      : %d\n", (int)ESP.getFlashChipMode());
    Serial.printf("sketch_size     : %u B\n", (unsigned)ESP.getSketchSize());
    Serial.printf("free_sketch     : %u B\n", (unsigned)ESP.getFreeSketchSpace());
    Serial.printf("psram_found     : %d\n", (int)psramFound());
    Serial.printf("psram_size      : %u B\n", (unsigned)ESP.getPsramSize());
    memline("baseline");

    Serial.println("--- WiFi STA init (sleep off) ---");
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    delay(300);
    memline("after wifi init");

    Serial.println("--- WiFi scan ---");
    int n = WiFi.scanNetworks(false, false);
    Serial.printf("scan_count      : %d\n", n);
    for (int i = 0; i < n; i++) {
        Serial.printf("  %2d | %-32s | %4d dBm | ch %2d | %s\n",
                      i, WiFi.SSID(i).c_str(), (int)WiFi.RSSI(i), (int)WiFi.channel(i),
                      WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "enc");
    }
    WiFi.scanDelete();
    delay(300);
    memline("after wifi scan");

    Serial.println("--- HTTP server lifecycle (port 80) ---");
    {
        WiFiServer srv(80);
        srv.begin();
        delay(300);
        memline("after server begin");
        srv.end();
        delay(300);
        memline("after server end");
    }

    Serial.println("--- allocation probe (after wifi+server churn) ---");
    alloc_probe(16 * 1024, 3);
    alloc_probe(20 * 1024, 3);
    alloc_probe(24 * 1024, 3);
    alloc_probe(32 * 1024, 3);
    memline("after alloc probe");

#ifdef CAMERA_PROBE
    camera_probe();
    memline("after camera probe");
#endif

    Serial.printf("report #%d done\n", g_report);
    Serial.println("=====================================");
    g_report++;
}

void setup() {
    Serial.begin(115200);
    unsigned long t0 = millis();
    while (!Serial && millis() - t0 < 3000) delay(10);
    delay(200);
    Serial.println();
    Serial.println("X5 ESP32 PROBE boot");
    full_report();
}

void loop() {
    static unsigned long last = 0;
    if (millis() - last >= 10000) {
        last = millis();
        full_report();
    }
    delay(300);
}
