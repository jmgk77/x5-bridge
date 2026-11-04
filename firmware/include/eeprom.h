#pragma once

#include <Arduino.h>

#include "config.h"

struct camera_config {
  uint8_t punch[24];
  char name[32];
  char ip[16];
  uint16_t port;
  bool enabled;
};

struct eeprom_data {
  uint32_t magic;
  uint16_t version;
  char device_name[32];
  char mqtt_server_ip[64];
  uint16_t mqtt_server_port;
  char mqtt_server_username[64];
  char mqtt_server_password[64];
  camera_config cameras[MAX_CAMERAS];
};

extern struct eeprom_data eeprom;

void default_eeprom();
void save_eeprom();
void init_eeprom();
