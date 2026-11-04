#include "main.h"

DNSServer dns;
AsyncWebServer server(80);
AsyncWiFiManager wm(&server, &dns);
ESPAsyncHTTPUpdateServer updateServer;

extern struct eeprom_data eeprom;

static String html_escape(const String& in) {
  String out;
  out.reserve(in.length());
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '&')
      out += "&amp;";
    else if (c == '<')
      out += "&lt;";
    else if (c == '>')
      out += "&gt;";
    else if (c == '"')
      out += "&quot;";
    else
      out += c;
  }
  return out;
}

static String sanitize_name(const String& in) {
  String out;
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-') {
      out += c;
    } else if (c == ' ' || c == '/' || c == '\\') {
      out += '_';
    }
  }
  if (out.length() == 0) out = DEFAULT_DEVICE_NAME;
  if (out.length() > 31) out = out.substring(0, 31);
  return out;
}

static String hex_encode(const uint8_t* data, size_t len) {
  static const char* hex = "0123456789abcdef";
  String out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; i++) {
    out += hex[(data[i] >> 4) & 0xf];
    out += hex[data[i] & 0xf];
  }
  return out;
}

static bool hex_decode(const String& hex, uint8_t* out, size_t max,
                       size_t* out_len) {
  if (hex.length() % 2 != 0) return false;
  size_t len = hex.length() / 2;
  if (len > max) return false;
  for (size_t i = 0; i < len; i++) {
    char hi = hex[2 * i];
    char lo = hex[2 * i + 1];
    auto nib = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    int a = nib(hi);
    int b = nib(lo);
    if (a < 0 || b < 0) return false;
    out[i] = (uint8_t)((a << 4) | b);
  }
  if (out_len) *out_len = len;
  return true;
}

static void schedule_restart(uint32_t delay_ms) {
  g_restart_at = millis() + delay_ms;
}

static String html_page(const String& title, const String& body,
                        bool refresh) {
  String h;
  h.reserve(body.length() + 512);
  h += "<!DOCTYPE html><html lang='pt-br'><head><meta charset='UTF-8'>";
  h += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  h += "<meta http-equiv='cache-control' content='no-cache, no-store, must-revalidate'>";
  if (refresh) h += "<meta http-equiv='refresh' content='10'>";
  h += "<title>" + html_escape(title) + "</title>";
  h += "<style>body{font-family:system-ui,-apple-system,'Segoe UI',Roboto,Arial,sans-serif;margin:16px;color:#202124}";
  h += "table{border-collapse:collapse;margin:8px 0}td,th{padding:4px 8px;border-bottom:1px solid #ccc;text-align:left}";
  h += "button{cursor:pointer;padding:6px 10px;margin:2px}code{background:#eee;padding:1px 4px}</style>";
  h += "</head><body>";
  h += body;
  h += "</body></html>";
  return h;
}

static String nav_buttons() {
  return "<p>"
         "<a href='/'><button>MAIN</button></a>"
         "<a href='/config'><button>CONFIG</button></a>"
         "<a href='/discover'><button>DISCOVER</button></a>"
         "<a href='/update'><button>UPDATE</button></a>"
         "<a href='/info'><button>INFO</button></a>"
         "<a href='/logs'><button>LOGS</button></a>"
         "<a href='/reboot'><button>REBOOT</button></a>"
         "<a href='/reset'><button>RESET</button></a>"
         "</p>";
}

static String camera_table() {
  String s = "<h3>Cameras</h3>";
  s += "<table><tr><th>Slot</th><th>Name</th><th>IP</th><th>Status</th>";
  s += "<th>Frames</th><th>FPS</th><th>Last frame</th><th>Clients</th>";
  s += "<th>Stream</th></tr>";

  String ip = WiFi.localIP().toString();
  for (int i = 0; i < MAX_CAMERAS; i++) {
    CameraStatus st;
    camera_get_status(i, &st);
    s += "<tr>";
    s += "<td>CAM" + String(i) + "</td>";
    s += "<td>" + html_escape(String(st.name)) + "</td>";
    s += "<td>" + html_escape(String(st.ip)) + "</td>";
    s += "<td>" + String(st.online ? "ONLINE" : "OFFLINE") + "</td>";
    s += "<td>" + String(st.frames) + "</td>";
    s += "<td>" + String(st.fps, 1) + "</td>";
    if (st.last_frame_ms) {
      s += "<td>" + String(st.last_frame_ms) + " ms</td>";
    } else {
      s += "<td>-</td>";
    }
    s += "<td>" + String(st.clients) + "</td>";
    if (st.configured) {
      s += "<td><a href='http://" + ip + ":" + String(st.port) +
           "/video'>video</a> / <a href='http://" + ip + ":" +
           String(st.port) + "/snapshot'>snapshot</a></td>";
    } else {
      s += "<td>-</td>";
    }
    s += "</tr>";
  }
  s += "</table>";
  return s;
}

static String status_page() {
  String s;
  s += "<h1>X5 BRIDGE</h1>";
  s += "<p>Device: <b>" + html_escape(String(eeprom.device_name)) + "</b><br>";
  s += "Version: " + String(VERSION) + "<br>";
  s += "IP: " + WiFi.localIP().toString() + "<br>";
  s += "MAC: " + WiFi.macAddress() + "<br>";
  s += "Uptime: " + String(millis() / 1000) + " s<br>";
  s += "Heap free/min: " + String(ESP.getFreeHeap()) + " / " +
       String(ESP.getMinFreeHeap()) + " bytes<br>";
  s += "RSSI: " + String(WiFi.RSSI()) + " dBm<br>";
  s += "MQTT broker: " + html_escape(String(eeprom.mqtt_server_ip)) + ":" +
       String(eeprom.mqtt_server_port) + "<br>";
  s += "mDNS: " + html_escape(String(eeprom.device_name)) + ".local</p>";
  s += nav_buttons();
  s += camera_table();
  return html_page("X5 BRIDGE", s, true);
}

static String config_page() {
  String s;
  s += "<h1>Config</h1>";
  s += "<form method='post' action='/config'>";
  s += "<input type='hidden' name='s' value='1'>";
  s += "<p>Device name (MQTT root): <input type='text' name='device_name' value='" +
       html_escape(String(eeprom.device_name)) + "'></p>";
  s += "<p>MQTT Broker: <input type='text' name='mqtt_server_ip' value='" +
       html_escape(String(eeprom.mqtt_server_ip)) + "'> ";
  s += "Port: <input type='number' name='mqtt_server_port' value='" +
       String(eeprom.mqtt_server_port) + "'></p>";
  s += "<p>MQTT username: <input type='text' name='mqtt_server_username' value='" +
       html_escape(String(eeprom.mqtt_server_username)) + "'><br>";
  s += "MQTT password: <input type='text' name='mqtt_server_password' value='' placeholder='" +
       String(eeprom.mqtt_server_password[0] ? "(mantido)" : "(vazio)") +
       "'> <label><input type='checkbox' name='clear_mqtt_password'> limpar</label></p>";

  for (int i = 0; i < MAX_CAMERAS; i++) {
    s += "<h3>CAM" + String(i) + " (port " +
         String(CAMERA_BASE_PORT + i) + ")</h3>";
    s += "<p>Name: <input type='text' name='cam" + String(i) +
         "_name' value='" + html_escape(String(eeprom.cameras[i].name)) +
         "'> ";
    s += "IP: <input type='text' name='cam" + String(i) + "_ip' value='" +
         html_escape(String(eeprom.cameras[i].ip)) + "'> ";
    s += "<label><input type='checkbox' name='cam" + String(i) +
         "_enabled' " + String(eeprom.cameras[i].enabled ? "checked" : "") +
         "> enabled</label></p>";
    if (eeprom.cameras[i].punch[0]) {
      s += "<p><small>punch: <code>" +
           hex_encode(eeprom.cameras[i].punch, 24) + "</code></small></p>";
    }
  }

  s += "<p><input type='submit' value='SALVAR'></p>";
  s += "</form>";
  s += "<p>Max active cameras: " + String(MAX_ACTIVE_CAMERAS) +
       ". Extra enabled slots are ignored.</p>";
  s += "<p><a href='/discover'><button>DESCOBRIR CAMERAS</button></a></p>";
  s += nav_buttons();
  return html_page("X5 BRIDGE - CONFIG", s, false);
}

static void handle_config(AsyncWebServerRequest* request) {
  if (!request->hasParam("s", true)) {
    request->send(200, "text/html", config_page());
    return;
  }

  String new_name = sanitize_name(
      request->hasParam("device_name", true)
          ? request->getParam("device_name", true)->value()
          : String(DEFAULT_DEVICE_NAME));
  strlcpy(eeprom.device_name, new_name.c_str(), sizeof(eeprom.device_name));

  if (request->hasParam("mqtt_server_ip", true)) {
    strlcpy(eeprom.mqtt_server_ip,
            request->getParam("mqtt_server_ip", true)->value().c_str(),
            sizeof(eeprom.mqtt_server_ip));
  }
  if (request->hasParam("mqtt_server_port", true)) {
    int port = request->getParam("mqtt_server_port", true)->value().toInt();
    if (port > 0 && port <= 65535) eeprom.mqtt_server_port = (uint16_t)port;
  }
  if (request->hasParam("mqtt_server_username", true)) {
    strlcpy(eeprom.mqtt_server_username,
            request->getParam("mqtt_server_username", true)->value().c_str(),
            sizeof(eeprom.mqtt_server_username));
  }
  if (request->hasParam("mqtt_server_password", true)) {
    String pw = request->getParam("mqtt_server_password", true)->value();
    if (pw.length() > 0) {
      strlcpy(eeprom.mqtt_server_password, pw.c_str(),
              sizeof(eeprom.mqtt_server_password));
    }
  }
  if (request->hasParam("clear_mqtt_password", true)) {
    eeprom.mqtt_server_password[0] = '\0';
  }

  int enabled = 0;
  for (int i = 0; i < MAX_CAMERAS; i++) {
    String name_key = "cam" + String(i) + "_name";
    String ip_key = "cam" + String(i) + "_ip";
    String en_key = "cam" + String(i) + "_enabled";

    if (request->hasParam(name_key, true)) {
      strlcpy(eeprom.cameras[i].name,
              request->getParam(name_key, true)->value().c_str(),
              sizeof(eeprom.cameras[i].name));
    }
    if (request->hasParam(ip_key, true)) {
      strlcpy(eeprom.cameras[i].ip,
              request->getParam(ip_key, true)->value().c_str(),
              sizeof(eeprom.cameras[i].ip));
    }

    eeprom.cameras[i].port = CAMERA_BASE_PORT + i;
    eeprom.cameras[i].enabled = request->hasParam(en_key, true);

    if (eeprom.cameras[i].enabled) {
      if (enabled < MAX_ACTIVE_CAMERAS) {
        enabled++;
      } else {
        eeprom.cameras[i].enabled = false;
      }
    }
  }

  save_eeprom();
  request->send(200, "text/html",
                "<meta http-equiv='refresh' content='2; url=/'>"
                "<html><body><p>Config salva. Reiniciando...</p></body></html>");
  schedule_restart(1500);
}

static void handle_discover(AsyncWebServerRequest* request) {
  DiscoveredCamera found[MAX_CAMERAS * 2];
  int n = discover_scan(found, MAX_CAMERAS * 2);

  String s;
  s += "<h1>Descobrir cameras</h1>";
  if (n == 0) {
    s += "<p>Nenhuma camera encontrada. Verifique a rede ou informe o IP em "
         "<a href='/config'>CONFIG</a>.</p>";
  } else {
    s += "<p>Encontradas: " + String(n) + "</p>";
    for (int i = 0; i < n; i++) {
      s += "<form method='post' action='/assign_camera'>";
      s += "<p><b>" + html_escape(String(found[i].name)) + "</b> - " +
           html_escape(String(found[i].ip)) + "<br>";
      s += "<input type='hidden' name='ip' value='" +
           html_escape(String(found[i].ip)) + "'>";
      s += "<input type='hidden' name='name' value='" +
           html_escape(String(found[i].name)) + "'>";
      s += "<input type='hidden' name='punch' value='" +
           hex_encode(found[i].punch, 24) + "'>";
      s += "Slot: <select name='slot'>";
      for (int k = 0; k < MAX_CAMERAS; k++) {
        s += "<option value='" + String(k) + "'>CAM" + String(k) + " (port " +
             String(CAMERA_BASE_PORT + k) + ")</option>";
      }
      s += "</select> <input type='submit' value='ATRIBUIR'></p>";
      s += "</form>";
    }
  }
  s += "<p><a href='/config'><button>VOLTAR</button></a></p>";
  s += nav_buttons();
  request->send(200, "text/html",
                html_page("X5 BRIDGE - DISCOVER", s, false));
}

static void handle_assign(AsyncWebServerRequest* request) {
  if (!request->hasParam("slot", true) || !request->hasParam("ip", true)) {
    request->send(400, "text/plain", "missing args");
    return;
  }

  int slot = request->getParam("slot", true)->value().toInt();
  if (slot < 0 || slot >= MAX_CAMERAS) {
    request->send(400, "text/plain", "bad slot");
    return;
  }

  String ip = request->getParam("ip", true)->value();
  strlcpy(eeprom.cameras[slot].ip, ip.c_str(), sizeof(eeprom.cameras[slot].ip));

  if (request->hasParam("name", true)) {
    strlcpy(eeprom.cameras[slot].name,
            request->getParam("name", true)->value().c_str(),
            sizeof(eeprom.cameras[slot].name));
  }
  if (request->hasParam("punch", true)) {
    size_t len = 0;
    uint8_t punch[24];
    if (hex_decode(request->getParam("punch", true)->value(), punch,
                   sizeof(punch), &len) &&
        len == 24) {
      memcpy(eeprom.cameras[slot].punch, punch, 24);
    }
  }

  eeprom.cameras[slot].port = CAMERA_BASE_PORT + slot;
  eeprom.cameras[slot].enabled = true;

  int enabled = 0;
  for (int i = 0; i < MAX_CAMERAS; i++) {
    if (eeprom.cameras[i].enabled) enabled++;
  }
  if (enabled > MAX_ACTIVE_CAMERAS) {
    eeprom.cameras[slot].enabled = false;
  }

  save_eeprom();
  request->send(200, "text/html",
                "<meta http-equiv='refresh' content='2; url=/'>"
                "<html><body><p>Camera atribuida. Reiniciando...</p></body></html>");
  schedule_restart(1500);
}

static void handle_info(AsyncWebServerRequest* request) {
  String s = "<h1>Info</h1>";
  s += html_dump_esp32();
  s += "<hr>";
  s += html_dump_fs();
  s += "<hr>";
  s += html_dump_config();
  s += nav_buttons();
  request->send(200, "text/html", html_page("X5 BRIDGE - INFO", s, false));
}

static void handle_logs(AsyncWebServerRequest* request) {
  if (request->hasArg("clear")) clear_logs();
  String s = "<h1>Logs</h1>";
  s += "<p><a href='/logs?clear=1'><button>LIMPAR</button></a>"
       "<a href='/logs'><button>ATUALIZAR</button></a></p>";
  s += "<pre style='background:#222;color:#0f0;padding:10px;max-height:70vh;"
       "overflow:auto;white-space:pre-wrap'>";
  s += html_escape(get_logs());
  s += "</pre>";
  s += nav_buttons();
  request->send(200, "text/html", html_page("X5 BRIDGE - LOGS", s, false));
}

static void handle_reboot(AsyncWebServerRequest* request) {
  request->send(200, "text/html",
                "<meta http-equiv='refresh' content='5; url=/'>"
                "<html><body><p>Reiniciando...</p></body></html>");
  schedule_restart(1500);
}

static void factory_reset_task(void* arg) {
  (void)arg;
  vTaskDelay(pdMS_TO_TICKS(1500));
  LittleFS.format();
  default_eeprom();
  save_eeprom();
  wm.resetSettings();
  ESP.restart();
}

static void handle_reset(AsyncWebServerRequest* request) {
  request->send(200, "text/html",
                "<meta http-equiv='refresh' content='5; url=/'>"
                "<html><body><p>Resetando para padroes de fabrica...</p></body></html>");
  xTaskCreate(factory_reset_task, "factory_reset", 4096, NULL, 1, NULL);
}

static void handle_not_found(AsyncWebServerRequest* request) {
  request->send(404, "text/html", "<p>Not found</p>");
}

void init_web() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  WiFi.hostname(eeprom.device_name);

  wm.setDebugOutput(false);
  wm.setConfigPortalTimeout(180);

  xlog_printf("WIFI connecting using '%s'...\n", eeprom.device_name);
  if (!wm.autoConnect(eeprom.device_name)) {
    xlog_println("WIFI FAILED, RESTARTING");
    ESP.restart();
    delay(1000);
  }

  xlog_printf("WIFI OK: %s\n", WiFi.localIP().toString().c_str());

  String host = String(eeprom.device_name);
  host.toLowerCase();
  if (MDNS.begin(host.c_str())) {
    MDNS.addService("http", "tcp", 80);
    xlog_printf("MDNS OK: %s.local\n", host.c_str());
  } else {
    xlog_println("MDNS FAILED");
  }

  server.onNotFound(handle_not_found);
  server.on("/", HTTP_GET,
            [](AsyncWebServerRequest* request) {
              request->send(200, "text/html", status_page());
            });
  server.on("/config", HTTP_ANY, handle_config);
  server.on("/discover", HTTP_GET, handle_discover);
  server.on("/assign_camera", HTTP_POST, handle_assign);
  server.on("/info", HTTP_GET, handle_info);
  server.on("/logs", HTTP_GET, handle_logs);
  server.on("/reboot", HTTP_ANY, handle_reboot);
  server.on("/reset", HTTP_ANY, handle_reset);

  updateServer.setup(&server, "/update");
  server.begin();
  xlog_println("WEB STARTED");
}

void handle_web() {}
