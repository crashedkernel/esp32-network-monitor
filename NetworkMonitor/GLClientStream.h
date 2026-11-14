#pragma once
#include "OpenWrtData.h"
#include <stdlib.h>
#include <strings.h>

// Only compact client records cross SSH. Traffic histories stay on the router.
class GLClientStream {
  RouterSnapshot &snapshot;
  char line[1536] = {};
  unsigned used = 0, records = 0;
  bool started = false, done = false, failed = false;
  const char *problem = "Incomplete GL.iNet client list";
  bool fail(const char *why) {
    failed = true;
    problem = why;
    return false;
  }
  bool endLine() {
    line[used] = 0;
    used = 0;
    if (!started) {
      if (strcmp(line, "NM2_GLCLIENTS"))
        return fail("Missing GL.iNet client-list header");
      started = true;
      return true;
    }
    if (done)
      return !*line || fail("Unexpected output after GL.iNet client list");
    if (!strncmp(line, "NM2_DONE ", 9)) {
      const char *p = line + 9;
      if (!*p)
        return fail("Missing GL.iNet record count");
      unsigned count = 0;
      for (; *p; p++) {
        if (*p < '0' || *p > '9' || count > 100000)
          return fail("Invalid GL.iNet record count");
        count = count * 10 + (*p - '0');
      }
      if (count != records)
        return fail("Truncated GL.iNet client list");
      done = true;
      return true;
    }
    cJSON *row = cJSON_Parse(line);
    const cJSON *mac = owItem(row, "mac"), *ip = owItem(row, "ip"), *name = owItem(row, "name"),
                *hostname = owItem(row, "hostname"), *online = owItem(row, "online"), *connection = owItem(row, "connection");
    bool valid = cJSON_IsObject(row) && cJSON_IsString(mac) && owMac(owText(mac)) &&
                 cJSON_IsString(ip) && cJSON_IsString(name) && cJSON_IsBool(online) &&
                 cJSON_IsString(connection) && (!hostname || cJSON_IsString(hostname));
    if (!valid) {
      cJSON_Delete(row);
      return fail("Invalid GL.iNet client record or online flag");
    }
    uint32_t address = 0;
    const char *ipText = owText(ip);
    if (*ipText && strcmp(ipText, "0.0.0.0") && !owIP(ipText, address)) {
      cJSON_Delete(row);
      return fail("Invalid GL.iNet IPv4 address");
    }
    Device *d = snapshot.client(owText(mac), address);
    if (!d) {
      cJSON_Delete(row);
      return fail(snapshot.allocationFailed ? "Not enough memory for GL.iNet clients"
                                            : "GL.iNet client list exceeds 254 stored devices");
    }
    d->online = cJSON_IsTrue(online);
    d->reported = true;
    // Use the alias/name selected by GL.iNet; do not replace it with DNS guesses.
    owCopy(d->name, owText(name), sizeof(d->name));
    if (strcmp(owText(hostname), "*"))
      owCopy(d->hostname, owText(hostname), sizeof(d->hostname));
    owCopy(d->connection, owText(connection), sizeof(d->connection));
    records++;
    cJSON_Delete(row);
    return true;
  }

public:
  explicit GLClientStream(RouterSnapshot &s) : snapshot(s) {}
  const char *error() const { return problem; }
  const char *phase() const {
    return !started ? "starting" : done ? "complete" : "GL.iNet clients";
  }
  bool feed(char c) {
    if (failed)
      return false;
    if (c == '\n')
      return endLine();
    if (((unsigned char)c < 32 && c != '\t' && c != '\r') || c == 127)
      return fail("Invalid character in GL.iNet output");
    if (used + 1 >= sizeof(line))
      return fail("GL.iNet client record too long");
    line[used++] = c;
    return true;
  }
  bool finish() {
    if (failed || (used && !endLine()))
      return false;
    return done || fail("GL.iNet client list ended before completion marker");
  }
};
