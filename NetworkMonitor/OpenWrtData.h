#pragma once
// Router data interpretation, kept independent of Arduino for fixture tests.
#include "Device.h"
#include <cJSON.h>
#include <ctype.h>
#include <new>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

inline const cJSON *owItem(const cJSON *o, const char *key) {
  return cJSON_GetObjectItemCaseSensitive(o, key);
}
inline const char *owText(const cJSON *v) { return cJSON_IsString(v) ? v->valuestring : ""; }
inline bool owIP(const char *s, uint32_t &ip) {
  ip = 0;
  for (int part = 0; part < 4; part++) {
    unsigned octet = 0, digits = 0;
    while (*s >= '0' && *s <= '9') {
      octet = octet * 10 + (*s++ - '0');
      if (++digits > 3)
        return false;
    }
    if (!digits || octet > 255)
      return false;
    ip = (ip << 8) | octet;
    if (part < 3) {
      if (*s++ != '.')
        return false;
    } else if (*s)
      return false;
  }
  return (ip >> 24) > 0 && (ip >> 24) < 224;
}

inline bool owMac(const char *s) {
  if (strlen(s) != 17)
    return false;
  for (int i = 0; i < 17; i++)
    if (i % 3 == 2 ? s[i] != ':' : !isxdigit((unsigned char)s[i]))
      return false;
  return true;
}
inline void owCopy(char *out, const char *in, size_t n) { snprintf(out, n, "%s", in); }
struct RouterSnapshot {
  Device *rows;
  unsigned count, capacity;
  uint32_t lo, hi;
  bool overflow = false;
  bool canGrow = false, allocationFailed = false;
  bool expand() {
    if (!canGrow || capacity >= 254)
      return false;
    unsigned next = capacity ? capacity * 2 : 16;
    if (next > 254)
      next = 254;
    Device *expanded = new (std::nothrow) Device[next];
    if (!expanded) {
      allocationFailed = true;
      return false;
    }
    memcpy(expanded, rows, count * sizeof(Device));
    delete[] rows;
    rows = expanded;
    capacity = next;
    return true;
  }
  Device *host(uint32_t ip, const char *mac = "", const char *name = "") {
    if (ip < lo || ip > hi)
      return nullptr;
    unsigned i = 0;
    for (; i < count; i++)
      if (rows[i].ip == ip)
        break;
    if (i == count) {
      if (count == capacity && !expand()) {
        overflow = true;
        return nullptr;
      }
      rows[count] = Device();
      rows[count].ip = ip;
      rows[count].isNew = true;
      count++;
    }
    Device &d = rows[i];
    if (owMac(mac)) {
      owCopy(d.mac, mac, sizeof(d.mac));
      for (char *p = d.mac; *p; p++)
        *p = toupper((unsigned char)*p);
    }
    if (*name && strcmp(name, "*"))
      owCopy(d.name, name, sizeof(d.name));
    return &d;
  }
  Device *client(const char *mac, uint32_t ip) {
    unsigned i = 0;
    for (; i < count; i++)
      if (!strcasecmp(rows[i].mac, mac))
        break;
    if (i == count) {
      if (count == capacity && !expand()) {
        overflow = true;
        return nullptr;
      }
      rows[count] = Device();
      rows[count].isNew = true;
      count++;
    }
    Device &d = rows[i];
    if (ip)
      d.ip = ip;
    owCopy(d.mac, mac, sizeof(d.mac));
    for (char *p = d.mac; *p; p++)
      *p = toupper((unsigned char)*p);
    return &d;
  }
  bool hints(const cJSON *data) {
    if (!cJSON_IsObject(data))
      return false;
    for (const cJSON *h = data->child; h; h = h->next) {
      if (!h->string || !owMac(h->string) || !cJSON_IsObject(h))
        return false;
      const cJSON *ips = owItem(h, "ipaddrs");
      if (!cJSON_IsArray(ips))
        return false;
      for (const cJSON *a = ips->child; a; a = a->next) {
        uint32_t ip;
        if (owIP(owText(a), ip))
          host(ip, h->string, owText(owItem(h, "name")));
      }
    }
    return !overflow;
  }
  bool leases(const cJSON *data) {
    const cJSON *leases = owItem(data, "dhcp_leases");
    if (!cJSON_IsArray(leases))
      return false;
    for (const cJSON *l = leases->child; l; l = l->next) {
      if (!cJSON_IsObject(l))
        return false;
      uint32_t ip;
      if (owIP(owText(owItem(l, "ipaddr")), ip))
        host(ip, owText(owItem(l, "macaddr")), owText(owItem(l, "hostname")));
    }
    return !overflow;
  }
  bool neighbors(const char *text) {
    // ip -4 neigh show: IP dev IFACE lladdr MAC STATE
    while (*text) {
      const char *end = strchr(text, '\n');
      size_t len = end ? size_t(end - text) : strlen(text);
      if (len >= 256)
        return false;
      char line[256];
      memcpy(line, text, len);
      line[len] = 0;
      char ipText[48] = {}, mac[18] = {};
      char *save = nullptr;
      char *token = strtok_r(line, " \t\r", &save);
      if (token)
        owCopy(ipText, token, sizeof(ipText));
      bool reachable = false, wantMac = false;
      while ((token = strtok_r(nullptr, " \t\r", &save))) {
        if (wantMac) {
          owCopy(mac, token, sizeof(mac));
          wantMac = false;
        }
        if (!strcmp(token, "lladdr"))
          wantMac = true;
        if (!strcmp(token, "REACHABLE"))
          reachable = true;
      }
      uint32_t ip;
      if (owIP(ipText, ip)) {
        Device *d = host(ip, mac);
        if (d && reachable)
          d->online = true;
      } else if (len)
        return false;
      text = end ? end + 1 : text + len;
    }
    return !overflow;
  }
  bool clients(const cJSON *data) {
    const cJSON *clients = owItem(data, "clients");
    if (!cJSON_IsObject(clients))
      return false;
    for (const cJSON *c = clients->child; c; c = c->next) {
      if (!c->string || !owMac(c->string) || !cJSON_IsObject(c))
        return false;
      if (!cJSON_IsTrue(owItem(c, "assoc")) || !cJSON_IsTrue(owItem(c, "authorized")))
        continue;
      char mac[18];
      owCopy(mac, c->string, sizeof(mac));
      for (char *p = mac; *p; p++)
        *p = toupper((unsigned char)*p);
      for (unsigned i = 0; i < count; i++)
        if (!strcmp(rows[i].mac, mac))
          rows[i].online = true;
    }
    return true;
  }
};
