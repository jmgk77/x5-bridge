#pragma once

#include <Arduino.h>

void init_mqtt();
void handle_mqtt();
void mqtt_publish_status();
bool mqtt_connected();
