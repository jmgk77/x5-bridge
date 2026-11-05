#include "main.h"

static const char* reset_reason_to_string(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "POWERON";
    case ESP_RST_EXT:
      return "EXTERNAL";
    case ESP_RST_SW:
      return "SOFTWARE";
    case ESP_RST_PANIC:
      return "PANIC";
    case ESP_RST_INT_WDT:
      return "INTERRUPT WATCHDOG";
    case ESP_RST_TASK_WDT:
      return "TASK WATCHDOG";
    case ESP_RST_WDT:
      return "WATCHDOG";
    case ESP_RST_DEEPSLEEP:
      return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:
      return "BROWNOUT";
    case ESP_RST_SDIO:
      return "SDIO";
    default:
      return "UNKNOWN";
  }
}

String html_dump_esp32() {
  char buf[1200];
  const esp_reset_reason_t reset_reason = esp_reset_reason();
  snprintf(
      buf, sizeof(buf),
      "ESP32<br>"
      "model: %s<br>"
      "revision: %d<br>"
      "CPU freq: %d MHz<br>"
      "SDK version: %s<br>"
      "reset reason: %s (%d)<br>"
      "heap free: %u bytes<br>"
      "heap total: %u bytes<br>"
      "heap min free: %u bytes<br>"
      "flash size: %u bytes<br>"
      "flash speed: %u Hz<br>"
      "sketch size: %u bytes<br>"
      "free sketch space: %u bytes<br>"
      "PSRAM size: %u bytes<br>"
      "PSRAM free: %u bytes<br>",
      ESP.getChipModel(), (int)ESP.getChipRevision(), (int)ESP.getCpuFreqMHz(),
      ESP.getSdkVersion(), reset_reason_to_string(reset_reason),
      (int)reset_reason, (unsigned)ESP.getFreeHeap(),
      (unsigned)ESP.getHeapSize(), (unsigned)ESP.getMinFreeHeap(),
      (unsigned)ESP.getFlashChipSize(), (unsigned)ESP.getFlashChipSpeed(),
      (unsigned)ESP.getSketchSize(), (unsigned)ESP.getFreeSketchSpace(),
      (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreePsram());
  return String(buf);
}

String html_dump_fs() {
  char buf[256];
  snprintf(buf, sizeof(buf),
           "FS<br>"
           "totalBytes: %u<br>"
           "usedBytes: %u<br>"
           "free bytes: %u<br>",
           (unsigned)LittleFS.totalBytes(), (unsigned)LittleFS.usedBytes(),
           (unsigned)(LittleFS.totalBytes() - LittleFS.usedBytes()));
  return String(buf);
}

String html_dump_config() {
  String s = "CONFIG<br>";
  s += "device_name: " + String(eeprom.device_name) + "<br>";
  s += "mqtt_server_ip: " + String(eeprom.mqtt_server_ip) + "<br>";
  s += "mqtt_server_port: " + String(eeprom.mqtt_server_port) + "<br>";
  s += "mqtt_server_username: " + String(eeprom.mqtt_server_username) + "<br>";
  s += "mqtt_server_password: " + String(eeprom.mqtt_server_password[0] ? "***" : "") + "<br>";

  for (int i = 0; i < MAX_CAMERAS; i++) {
    s += "CAM" + String(i) + ": ";
    s += "name=" + String(eeprom.cameras[i].name) + " ";
    s += "ip=" + String(eeprom.cameras[i].ip) + " ";
    s += "port=" + String(eeprom.cameras[i].port) + " ";
    s += "resolution=" +
         String(eeprom.cameras[i].resolution == CAMERA_RES_320X240 ? "320x240"
                                                                   : "640x480") +
         " ";
    s += "enabled=" + String(eeprom.cameras[i].enabled ? "true" : "false");
    s += "<br>";
  }
  return s;
}
