#pragma once
#include "TimeZones.h"
#include <ctype.h>
#include <string.h>

constexpr const char DEFAULT_ADMIN_PASSWORD[] = "netmonadmin123";
constexpr const char DEFAULT_AP_PASSWORD[] = "netmon123";
constexpr const char DEFAULT_TIME_ZONE[] = "America/New_York";
constexpr const char DEFAULT_TIME_SERVER[] = "time.nist.gov";

inline bool validTimeServer(const char *server) {
  size_t length = strlen(server);
  if (!length || length > 253)
    return false;
  size_t label = 0;
  char previous = 0;
  for (const char *p = server; *p; p++) {
    unsigned char c = *p;
    if (c == '.') {
      if (!label || previous == '-')
        return false;
      label = 0;
    } else {
      if (!(isalnum(c) || c == '-') || (!label && c == '-') || ++label > 63)
        return false;
    }
    previous = c;
  }
  return label && previous != '-';
}
