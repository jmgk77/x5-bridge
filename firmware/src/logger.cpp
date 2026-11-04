#include "main.h"

static String ram_log_buffer = "";
static bool fs_ready = false;

static void append_to_file(const char* str) {
  if (!fs_ready) {
    fs_ready = LittleFS.begin(true);
  }
  if (!fs_ready) return;

  if (LittleFS.exists(LOG_FILE_NAME)) {
    File check_file = LittleFS.open(LOG_FILE_NAME, "r");
    if (check_file) {
      size_t size = check_file.size();
      check_file.close();
      if (size > LOG_MAX_FILE_SIZE) {
        LittleFS.remove("/system.old.log");
        LittleFS.rename(LOG_FILE_NAME, "/system.old.log");
      }
    }
  }

  File f = LittleFS.open(LOG_FILE_NAME, "a");
  if (f) {
    f.print(str);
    f.close();
  }
}

static void append_to_ram(const char* str) {
  ram_log_buffer += str;
  if (ram_log_buffer.length() > RAM_LOG_BUFFER_SIZE) {
    int keep_start = ram_log_buffer.length() - RAM_LOG_BUFFER_SIZE;
    ram_log_buffer = ram_log_buffer.substring(keep_start);
  }
}

void init_logger() {
  fs_ready = LittleFS.begin(true);
  xlog_println("--- Logger initialized ---");
}

void xlog_printf(const char* format, ...) {
  char buf[512];
  va_list args;
  va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);

#ifdef DEBUG
  Serial.print(buf);
#endif

  append_to_ram(buf);
  append_to_file(buf);
}

void xlog_println(const char* msg) { xlog_printf("%s\n", msg); }
void xlog_println(const String& msg) { xlog_printf("%s\n", msg.c_str()); }

String get_logs() {
  if (!fs_ready) {
    fs_ready = LittleFS.begin(true);
  }
  if (fs_ready && LittleFS.exists(LOG_FILE_NAME)) {
    File f = LittleFS.open(LOG_FILE_NAME, "r");
    if (f) {
      String content = f.readString();
      f.close();
      return content;
    }
  }
  return ram_log_buffer;
}

void clear_logs() {
  ram_log_buffer = "";
  if (fs_ready || LittleFS.begin(true)) {
    LittleFS.remove(LOG_FILE_NAME);
    LittleFS.remove("/system.old.log");
  }
  xlog_println("--- Logs cleared ---");
}
