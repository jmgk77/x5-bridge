#include "main.h"

volatile uint32_t g_restart_at = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.print("X5 BRIDGE ");
  Serial.println(VERSION);

  init_eeprom();
  LittleFS.begin(true);
  init_logger();

  xlog_printf("=== START %s %s ===\n", DEFAULT_DEVICE_NAME, VERSION);

  init_web();
  init_mqtt();
  init_camera();
  init_stream();

  xlog_println("SETUP DONE");
}

void loop() {
  handle_web();
  handle_mqtt();

  if (g_restart_at != 0 && millis() >= g_restart_at) {
    ESP.restart();
  }

  vTaskDelay(pdMS_TO_TICKS(10));
}
