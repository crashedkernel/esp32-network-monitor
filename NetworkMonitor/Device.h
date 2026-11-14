#pragma once
#include <stdint.h>
struct Device {
  uint32_t ip = 0;
  char name[96] = {0};
  char hostname[96] = {0};
  char mac[18] = {0};
  bool online = false, changed = false, isNew = false, reported = false;
  uint32_t seen = 0;
  char connection[32] = {0};
};
