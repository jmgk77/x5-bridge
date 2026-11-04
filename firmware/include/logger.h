#pragma once

#include <Arduino.h>

#define LOG_FILE_NAME "/system.log"
#define LOG_MAX_FILE_SIZE 16384
#define RAM_LOG_BUFFER_SIZE 4096

void init_logger();
void xlog_printf(const char* format, ...);
void xlog_println(const String& msg);
void xlog_println(const char* msg);
String get_logs();
void clear_logs();
