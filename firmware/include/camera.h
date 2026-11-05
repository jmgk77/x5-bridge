#pragma once

#include <Arduino.h>

#include "config.h"

struct DiscoveredCamera {
  uint8_t punch[24];
  char name[32];
  char ip[16];
};

struct CameraStatus {
  bool configured;
  bool online;
  bool got_ticket;
  char name[32];
  char ip[16];
  uint16_t port;
  uint32_t frames;
  uint32_t dropped;
  uint32_t recovered;
  float fps;
  uint32_t last_frame_ms;
  int clients;
};

void init_camera();

int discover_scan(DiscoveredCamera* out, int max);
bool camera_get_status(int slot, CameraStatus* out);

void camera_lock();
void camera_unlock();
bool camera_frame_view(int slot, const uint8_t** ptr, size_t* len,
                       uint32_t* seq);
uint32_t camera_frame_seq(int slot);
void camera_set_clients(int slot, int n);

void camera_name_from_punch(const uint8_t* punch, char* out, size_t out_size);
