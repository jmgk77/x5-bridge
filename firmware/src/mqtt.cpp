#include "main.h"

static PicoMQTT::Client* mqtt_client = nullptr;
static bool g_connected = false;
static uint32_t last_announce = 0;

extern struct eeprom_data eeprom;

static String topic(const char* suffix) {
  return String(eeprom.device_name) + "/" + suffix;
}

static void publish_retained(const String& t, const String& payload) {
  if (!mqtt_client || !g_connected) return;
  mqtt_client->publish(t, payload, 0, true);
}

void mqtt_publish_status() {
  if (!mqtt_client || !g_connected) return;

  String ip = WiFi.localIP().toString();
  publish_retained(topic("DESCRIPTION"),
                   String(eeprom.device_name) + " " + String(VERSION));
  publish_retained(topic("IP"), ip);
  publish_retained(topic("STATUS"), "ONLINE");
  publish_retained(topic("ADMIN"), "http://" + ip + "/");
  publish_retained(topic("MAC"), WiFi.macAddress());
  publish_retained(topic("UPTIME"), String(millis() / 1000));
  publish_retained(topic("HEAP"), String(ESP.getFreeHeap()));
  publish_retained(topic("RSSI"), String(WiFi.RSSI()));

  for (int i = 0; i < MAX_CAMERAS; i++) {
    CameraStatus st;
    camera_get_status(i, &st);
    if (!st.configured) continue;

    String base = String(eeprom.device_name) + "/CAM" + String(i);
    String name = st.name[0] ? String(st.name) : String(st.ip);
    String url = "http://" + ip + ":" + String(st.port);

    publish_retained(base + "/DESCRIPTION", name);
    publish_retained(base + "/IP", String(st.ip));
    publish_retained(base + "/STATUS", st.online ? "ONLINE" : "OFFLINE");
    publish_retained(base + "/PORT", String(st.port));
    publish_retained(base + "/URL", url + "/video");
    publish_retained(base + "/SNAPSHOT", url + "/snapshot");
    publish_retained(base + "/CLIENTS", String(st.clients));
    publish_retained(base + "/FPS", String(st.fps, 1));
    publish_retained(base + "/LAST_FRAME", String(st.last_frame_ms));
  }

  xlog_printf("MQTT ANNOUNCE OK (heap=%u)\n", (unsigned)ESP.getFreeHeap());
}

void init_mqtt() {
  if (eeprom.mqtt_server_ip[0] == '\0') {
    xlog_println("MQTT DISABLED (empty broker)");
    return;
  }

  mqtt_client = new PicoMQTT::Client(
      eeprom.mqtt_server_ip, eeprom.mqtt_server_port, eeprom.device_name,
      eeprom.mqtt_server_username, eeprom.mqtt_server_password);

  mqtt_client->will.topic = topic("STATUS");
  mqtt_client->will.payload = "OFFLINE";
  mqtt_client->will.qos = 0;
  mqtt_client->will.retain = true;

  mqtt_client->connected_callback = [] {
    g_connected = true;
    xlog_println("MQTT CONNECTED");
    mqtt_publish_status();
  };
  mqtt_client->disconnected_callback = [] {
    g_connected = false;
    xlog_println("MQTT DISCONNECTED");
  };

  mqtt_client->begin();
  last_announce = millis() - MQTT_ANNOUNCE_INTERVAL_MS;
  xlog_printf("MQTT START %s:%d\n", eeprom.mqtt_server_ip,
             eeprom.mqtt_server_port);
}

void handle_mqtt() {
  if (!mqtt_client) return;

  mqtt_client->loop();

  uint32_t now = millis();
  if (g_connected && now - last_announce >= MQTT_ANNOUNCE_INTERVAL_MS) {
    last_announce = now;
    mqtt_publish_status();
  }
}

bool mqtt_connected() { return g_connected; }
