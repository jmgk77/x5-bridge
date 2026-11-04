#pragma once

#define DEBUG

#include <Arduino.h>
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <ESPAsyncHTTPUpdateServer.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <PicoMQTT.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_chip_info.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

#include "camera.h"
#include "config.h"
#include "dump.h"
#include "eeprom.h"
#include "logger.h"
#include "mqtt.h"
#include "stream.h"
#include "version.h"
#include "web.h"
