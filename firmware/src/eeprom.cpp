#include "main.h"

#define EEPROM_MAGIC 0x58354252UL
#define EEPROM_VERSION 1

struct eeprom_data eeprom;
static Preferences preferences;

void default_eeprom() {
  memset(&eeprom, 0, sizeof(eeprom));
  eeprom.magic = EEPROM_MAGIC;
  eeprom.version = EEPROM_VERSION;

  strlcpy(eeprom.device_name, DEFAULT_DEVICE_NAME, sizeof(eeprom.device_name));
  strlcpy(eeprom.mqtt_server_ip, DEFAULT_MQTT_SERVER,
          sizeof(eeprom.mqtt_server_ip));
  eeprom.mqtt_server_port = DEFAULT_MQTT_PORT;

  for (int i = 0; i < MAX_CAMERAS; i++) {
    eeprom.cameras[i].port = CAMERA_BASE_PORT + i;
    eeprom.cameras[i].enabled = false;
    eeprom.cameras[i].resolution = DEFAULT_CAMERA_RESOLUTION;
  }
}

void save_eeprom() {
  preferences.begin("x5bridge", false);
  preferences.putBytes("config", &eeprom, sizeof(eeprom_data));
  preferences.end();
  xlog_println("CONFIG SAVED");
}

void init_eeprom() {
  preferences.begin("x5bridge", false);

  if (preferences.getBytesLength("config") == sizeof(eeprom_data)) {
    preferences.getBytes("config", &eeprom, sizeof(eeprom_data));
    if (eeprom.magic != EEPROM_MAGIC || eeprom.version != EEPROM_VERSION) {
      xlog_println("CONFIG INVALID, LOADING DEFAULTS");
      default_eeprom();
      preferences.putBytes("config", &eeprom, sizeof(eeprom_data));
    } else {
      xlog_println("CONFIG OK");
    }
  } else {
    xlog_println("CONFIG NOT FOUND, LOADING DEFAULTS");
    default_eeprom();
    preferences.putBytes("config", &eeprom, sizeof(eeprom_data));
  }

  bool changed = false;
  for (int i = 0; i < MAX_CAMERAS; i++) {
    if (eeprom.cameras[i].port < CAMERA_BASE_PORT ||
        eeprom.cameras[i].port >= CAMERA_BASE_PORT + MAX_CAMERAS) {
      eeprom.cameras[i].port = CAMERA_BASE_PORT + i;
      changed = true;
    }
    if (eeprom.cameras[i].resolution != CAMERA_RES_320X240 &&
        eeprom.cameras[i].resolution != CAMERA_RES_640X480) {
      eeprom.cameras[i].resolution = DEFAULT_CAMERA_RESOLUTION;
      changed = true;
    }
  }
  if (changed) preferences.putBytes("config", &eeprom, sizeof(eeprom_data));

  preferences.end();
}
