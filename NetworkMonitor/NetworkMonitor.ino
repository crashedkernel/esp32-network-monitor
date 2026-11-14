/* LuatOS ESP32-C3 network monitor. Arduino-ESP32 3.x; LibSSH-ESP32 5.10.0. */
#include "WebAssets.h"
#include "TimeSettings.h"
#include <Arduino.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <atomic>
#include <errno.h>
#include <esp_sntp.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <fcntl.h>
#include <libssh_esp32.h>
#include <lwip/etharp.h>
#include <lwip/inet.h>
#include <lwip/pbuf.h>
#include <lwip/sockets.h>
#include <lwip/tcpip.h>
#include <time.h>

constexpr unsigned MAX_HOSTS = 254;
uint32_t scanIntervalSeconds = 60;
WebServer server(80);
Preferences prefs;
String ssid, wifiPassword, adminPassword, mode, subnet, rangeStart, rangeEnd, csrf;
String scanSource = "local", routerIP, routerUser, routerPassword;
String routerTransport = "http";
uint16_t routerPort = 80;
String setupPassword;
uint8_t wifiChannel = 0;
uint8_t wifiWidth = 40;
String scanToken;
#if defined(ARDUINO_AirM2M_CORE_ESP32C3) || defined(RGB_BUILTIN)
bool activityLedEnabled = true;
#if defined(RGB_BUILTIN)
constexpr uint32_t DEFAULT_LED_COLOR = 0x004000;
uint32_t activityLedColor = DEFAULT_LED_COLOR;
#endif
#endif
std::atomic<uint32_t> wifiLinkGeneration{0};
String timeZone = DEFAULT_TIME_ZONE, timeFormat = "24", timeServer = DEFAULT_TIME_SERVER;
bool apRunning = false;
SemaphoreHandle_t lock;
#include "Device.h"
Device *devices = nullptr;
unsigned deviceCount = 0, progress = 0;
std::atomic<bool> scanning{false};
bool completed = false, scanRequested = false;
TaskHandle_t scannerTask = nullptr;
std::atomic<bool> clockHasSync{false};
uint64_t lastScanEpoch = 0;
void clockSyncCallback(struct timeval *) { clockHasSync.store(true); }
uint64_t utcNow() {
  time_t now = time(nullptr);
  return clockHasSync.load() && now >= 1700000000 ? uint64_t(now) : 0;
}
uint32_t lastScan = 0, restartAt = 0, scanNumber = 0;
String scanError;

uint32_t number(IPAddress ip) {
  return (uint32_t(ip[0]) << 24) | (uint32_t(ip[1]) << 16) | (uint32_t(ip[2]) << 8) | ip[3];
}
IPAddress address(uint32_t n) { return IPAddress(n >> 24, n >> 16, n >> 8, n); }
bool parseIP(String s, uint32_t &n) {
  IPAddress a;
  if (!a.fromString(s))
    return false;
  n = number(a);
  return true;
}
String escape(String s) {
  s.replace("&", "&amp;");
  s.replace("<", "&lt;");
  s.replace(">", "&gt;");
  s.replace("\"", "&quot;");
  s.replace("'", "&#39;");
  return s;
}
String json(String s) {
  String r = "\"";
  for (unsigned i = 0; i < s.length(); i++) {
    unsigned char c = s[i];
    if (c == '"' || c == '\\') {
      r += '\\';
      r += char(c);
    } else if (c >= 32)
      r += char(c);
    else {
      char code[7];
      snprintf(code, sizeof(code), "\\u%04x", c);
      r += code;
    }
  }
  return r + '"';
}
bool bounds(String m, String cidr, String first, String last, uint32_t &lo, uint32_t &hi) {
  if (m == "auto") {
    uint32_t ip = number(WiFi.localIP()), mask = number(WiFi.subnetMask());
    lo = (ip & mask) + 1;
    hi = (ip | ~mask) - 1;
  } else if (m == "subnet") {
    int slash = cidr.indexOf('/');
    if (slash < 0)
      return false;
    String bits = cidr.substring(slash + 1);
    if (bits.length() != 2 || bits.toInt() < 24 || bits.toInt() > 30)
      return false;
    uint32_t ip;
    if (!parseIP(cidr.substring(0, slash), ip))
      return false;
    uint32_t mask = 0xffffffffUL << (32 - bits.toInt());
    lo = (ip & mask) + 1;
    hi = (ip | ~mask) - 1;
  } else if (m == "range") {
    if (!parseIP(first, lo) || !parseIP(last, hi))
      return false;
  } else
    return false;
  return lo <= hi && uint64_t(hi) - lo + 1 <= MAX_HOSTS && (lo >> 24) != 0 && (hi >> 24) < 224;
}
uint16_t checksum(const uint8_t *p, size_t n) {
  uint32_t sum = 0;
  while (n > 1) {
    sum += (p[0] << 8) | p[1];
    p += 2;
    n -= 2;
  }
  if (n)
    sum += p[0] << 8;
  while (sum >> 16)
    sum = (sum & 65535) + (sum >> 16);
  return ~sum;
}
bool ping(uint32_t ip) {
  int fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (fd < 0)
    return false;
  timeval timeout = {0, 120000};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  uint16_t tag = uint16_t(esp_random());
  uint8_t packet[12] = {8, 0, 0, 0, uint8_t(tag >> 8), uint8_t(tag), 0, 1, 'N', 'M', 'O', 'N'};
  uint16_t c = checksum(packet, sizeof(packet));
  packet[2] = c >> 8;
  packet[3] = c;
  sockaddr_in target = {};
  target.sin_family = AF_INET;
  target.sin_addr.s_addr = htonl(ip);
  bool ok = false;
  if (sendto(fd, packet, sizeof(packet), 0, (sockaddr *)&target, sizeof(target)) ==
      sizeof(packet)) {
    uint32_t start = millis();
    uint8_t buf[256];
    while (millis() - start < 150) {
      sockaddr_in from = {};
      socklen_t len = sizeof(from);
      int n = recvfrom(fd, buf, sizeof(buf), 0, (sockaddr *)&from, &len);
      if (n < 0)
        break;
      unsigned h = (buf[0] & 15) * 4;
      if (h >= 20 && n >= int(h + 12) && from.sin_addr.s_addr == target.sin_addr.s_addr &&
          buf[h] == 0 && buf[h + 4] == packet[4] && buf[h + 5] == packet[5] && buf[h + 7] == 1) {
        ok = true;
        break;
      }
    }
  }
  close(fd);
  return ok;
}
// Observe fresh ARP replies before forwarding every frame to the normal stack.
// A previous scan's ARP cache entry must never be counted as current evidence.
portMUX_TYPE arpMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t arpTarget = 0, arpLocal = 0;
bool arpActive = false, arpAnswered = false;
uint8_t arpMac[6] = {};
err_t monitorInput(struct pbuf *p, struct netif *n) {
  uint8_t frame[42];
  if (pbuf_copy_partial(p, frame, sizeof(frame), 0) == sizeof(frame) && frame[12] == 8 &&
      frame[13] == 6 && frame[14] == 0 && frame[15] == 1 && frame[16] == 8 && frame[17] == 0 &&
      frame[18] == 6 && frame[19] == 4 && frame[20] == 0 && frame[21] == 2) {
    uint32_t sender = (uint32_t(frame[28]) << 24) | (uint32_t(frame[29]) << 16) |
                      (uint32_t(frame[30]) << 8) | frame[31];
    uint32_t target = (uint32_t(frame[38]) << 24) | (uint32_t(frame[39]) << 16) |
                      (uint32_t(frame[40]) << 8) | frame[41];
    portENTER_CRITICAL(&arpMux);
    if (arpActive && sender == arpTarget && target == arpLocal) {
      memcpy(arpMac, frame + 22, 6);
      arpAnswered = true;
    }
    portEXIT_CRITICAL(&arpMux);
  }
  return tcpip_input(p, n);
}
struct ArpRequest {
  uint32_t ip, local;
  bool sent;
  SemaphoreHandle_t done;
};
void requestArp(void *arg) {
  auto *r = (ArpRequest *)arg;
  struct netif *n;
  NETIF_FOREACH(n) {
    if (ntohl(netif_ip4_addr(n)->addr) != r->local)
      continue;
    // Only wrap the standard input function; leave custom network stacks alone.
    if (n->input != tcpip_input && n->input != monitorInput)
      break;
    n->input = monitorInput;
    ip4_addr_t ip;
    ip.addr = htonl(r->ip);
    portENTER_CRITICAL(&arpMux);
    arpTarget = r->ip;
    arpLocal = r->local;
    arpAnswered = false;
    arpActive = true;
    portEXIT_CRITICAL(&arpMux);
    r->sent = etharp_request(n, &ip) == ERR_OK;
    break;
  }
  xSemaphoreGive(r->done);
}
bool probeArp(uint32_t ip, char *mac) {
  uint32_t local = number(WiFi.localIP()), mask = number(WiFi.subnetMask());
  if ((ip & mask) != (local & mask))
    return false;
  ArpRequest req = {ip, local, false, xSemaphoreCreateBinary()};
  if (!req.done)
    return false;
  if (tcpip_callback(requestArp, &req) == ERR_OK)
    xSemaphoreTake(req.done, portMAX_DELAY);
  vSemaphoreDelete(req.done);
  bool answered = false;
  if (req.sent) {
    uint32_t start = millis();
    do {
      portENTER_CRITICAL(&arpMux);
      answered = arpAnswered;
      portEXIT_CRITICAL(&arpMux);
      if (answered)
        break;
      vTaskDelay(pdMS_TO_TICKS(5));
    } while (millis() - start < 120);
  }
  portENTER_CRITICAL(&arpMux);
  if (answered)
    snprintf(mac, 18, "%02X:%02X:%02X:%02X:%02X:%02X", arpMac[0], arpMac[1], arpMac[2], arpMac[3],
             arpMac[4], arpMac[5]);
  arpActive = false;
  portEXIT_CRITICAL(&arpMux);
  return answered;
}
bool probeTcp(uint32_t ip) {
  for (uint16_t port : {80, 443, 22}) {
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0)
      continue;
    if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
      close(fd);
      continue;
    }
    sockaddr_in target = {};
    target.sin_family = AF_INET;
    target.sin_addr.s_addr = htonl(ip);
    target.sin_port = htons(port);
    int rc = connect(fd, (sockaddr *)&target, sizeof(target));
    int error = rc < 0 ? errno : 0;
    bool online = rc == 0 || error == ECONNREFUSED;
    if (rc < 0 && error == EINPROGRESS) {
      fd_set writes, errors;
      FD_ZERO(&writes);
      FD_ZERO(&errors);
      FD_SET(fd, &writes);
      FD_SET(fd, &errors);
      timeval timeout = {0, 80000};
      if (select(fd + 1, nullptr, &writes, &errors, &timeout) > 0) {
        socklen_t size = sizeof(error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) == 0)
          online = error == 0 || error == ECONNREFUSED;
      }
    }
    close(fd);
    if (online)
      return true;
  }
  return false;
}
// Decode DNS labels, including compressed pointers, with strict bounds.
bool dnsName(const uint8_t *b, int n, int &pos, String &out) {
  int cur = pos, end = -1;
  out = "";
  for (int hops = 0; hops < 64; hops++) {
    if (cur >= n)
      return false;
    int l = b[cur++];
    if (!l) {
      pos = end < 0 ? cur : end;
      return true;
    }
    if ((l & 192) == 192) {
      if (cur >= n)
        return false;
      int ptr = ((l & 63) << 8) | b[cur++];
      if (end < 0)
        end = cur;
      cur = ptr;
      continue;
    }
    if (l > 63 || cur + l > n || out.length() + l > 95)
      return false;
    if (out.length())
      out += '.';
    while (l--)
      out += char(b[cur++]);
  }
  return false;
}
String reverseName(uint32_t ip, IPAddress dns, uint32_t waitMs) {
  WiFiUDP udp;
  if (!dns || !udp.begin(0))
    return "";
  uint16_t id = uint16_t(esp_random());
  uint8_t q[96] = {uint8_t(id >> 8), uint8_t(id), 1, 0, 0, 1, 0, 0, 0, 0, 0, 0};
  int len = 12;
  IPAddress a = address(ip);
  String host = String(a[3]) + "." + a[2] + "." + a[1] + "." + a[0] + ".in-addr.arpa";
  int start = 0;
  while (start < int(host.length())) {
    int dot = host.indexOf('.', start);
    if (dot < 0)
      dot = host.length();
    q[len++] = dot - start;
    for (int i = start; i < dot; i++)
      q[len++] = host[i];
    start = dot + 1;
  }
  q[len++] = 0;
  q[len++] = 0;
  q[len++] = 12;
  q[len++] = 0;
  q[len++] = 1;
  if (!udp.beginPacket(dns, 53) || udp.write(q, len) != size_t(len) || !udp.endPacket())
    return "";
  uint32_t since = millis();
  String result;
  while (millis() - since < waitMs) {
    int size = udp.parsePacket();
    if (size) {
      uint8_t b[512];
      int n = udp.read(b, sizeof(b));
      if (n < 12 || udp.remoteIP() != dns || udp.remotePort() != 53 ||
          b[0] != q[0] || b[1] != q[1] || !(b[2] & 128) || (b[2] & 2) || (b[3] & 15))
        continue;
      int p = 12;
      String ignored;
      int questions = (b[4] << 8) | b[5], answers = (b[6] << 8) | b[7];
      if (questions != 1 || !dnsName(b, n, p, ignored) || !ignored.equalsIgnoreCase(host) ||
          p + 4 > n || b[p] || b[p + 1] != 12 || b[p + 2] || b[p + 3] != 1)
        continue;
      p += 4;
      for (int i = 0; i < answers; i++) {
        if (!dnsName(b, n, p, ignored) || p + 10 > n)
          break;
        int type = (b[p] << 8) | b[p + 1], length = (b[p + 8] << 8) | b[p + 9];
        p += 10;
        if (p + length > n)
          break;
        if (type == 12 && !b[p - 8] && b[p - 7] == 1 && ignored.equalsIgnoreCase(host)) {
          int namePos = p;
          if (!dnsName(b, n, namePos, result) || namePos != p + length)
            result = "";
          break;
        }
        p += length;
      }
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  udp.stop();
  return result;
}
#include "OpenWrt.h"
void scanTask(void *) {
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      closeRouterSSH();
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    const uint32_t generation = wifiLinkGeneration.load();
    uint32_t began = millis(), lo, hi;
    if (scanSource == "glinet") {
      lo = 0;
      hi = 0xffffffffUL;
    } else if (!bounds(mode, subnet, rangeStart, rangeEnd, lo, hi)) {
      xSemaphoreTake(lock, portMAX_DELAY);
      scanError = "Choose a subnet or range with at most 254 addresses in Admin.";
      xSemaphoreGive(lock);
      vTaskDelay(pdMS_TO_TICKS(3000));
      continue;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    scanning = true;
    scanRequested = false;
    ulTaskNotifyTake(pdTRUE, 0);
    progress = 0;
    scanError = "";
    xSemaphoreGive(lock);
    // Allocate reports according to the actual inventory. Swap completed tables
    // under the mutex so readers never see a partially built report.
    unsigned capacity = scanSource == "glinet" ? 16 : hi - lo + 1;
    if (scanSource != "local") {
      xSemaphoreTake(lock, portMAX_DELAY);
      capacity = deviceCount + 8;
      xSemaphoreGive(lock);
      if (capacity < 16)
        capacity = 16;
      if (capacity > MAX_HOSTS)
        capacity = MAX_HOSTS;
    }
    Device *fresh = new (std::nothrow) Device[capacity];
    if (!fresh) {
      xSemaphoreTake(lock, portMAX_DELAY);
      scanning = false;
      scanError = "Not enough memory for a scan working table. Previous report "
                  "retained.";
      xSemaphoreGive(lock);
      vTaskDelay(pdMS_TO_TICKS(3000));
      continue;
    }
    unsigned count = 0;
    bool aborted = false;
    if (scanSource != "local") {
      // GL.iNet supplies a complete authoritative list; do not invent offline
      // rows for clients it has removed. Legacy OpenWrt retains prior rows.
      xSemaphoreTake(lock, portMAX_DELAY);
      count = scanSource == "glinet" ? 0 : deviceCount;
      if (count)
        memcpy(fresh, devices, count * sizeof(Device));
      xSemaphoreGive(lock);
      for (unsigned i = 0; i < count; i++) {
        fresh[i].online = false;
        fresh[i].changed = false;
        fresh[i].isNew = false;
      }
      RouterSnapshot snapshot = {fresh, count, capacity, lo, hi};
      snapshot.canGrow = true;
      String error;
      aborted = !readRouterSnapshot(snapshot, error);
      count = snapshot.count;
      fresh = snapshot.rows;
      if (!aborted && (WiFi.status() != WL_CONNECTED || wifiLinkGeneration.load() != generation)) {
        aborted = true;
        error = "Wi-Fi disconnected during router query.";
      }
      xSemaphoreTake(lock, portMAX_DELAY);
      if (aborted)
        scanError = error + " Previous report retained.";
      else {
        for (unsigned i = 0; i < count; i++) {
          for (unsigned j = 0; j < deviceCount; j++)
            if (scanSource == "glinet" ? !strcasecmp(fresh[i].mac, devices[j].mac)
                                       : fresh[i].ip == devices[j].ip) {
              fresh[i].isNew = false;
              if (!fresh[i].ip)
                fresh[i].ip = devices[j].ip;
              if (!fresh[i].hostname[0] && fresh[i].ip == devices[j].ip &&
                  !strcasecmp(fresh[i].mac, devices[j].mac))
                strlcpy(fresh[i].hostname, devices[j].hostname, sizeof(fresh[i].hostname));
              else if (scanSource == "openwrt" && strcasecmp(fresh[i].mac, devices[j].mac))
                fresh[i].hostname[0] = 0;
              fresh[i].changed = fresh[i].online != devices[j].online;
              break;
            }
          if (fresh[i].online)
            fresh[i].seen = millis();
        }
        progress = count;
      }
      xSemaphoreGive(lock);
    } else
      for (uint32_t ip = lo; ip <= hi; ip++) {
        if (WiFi.status() != WL_CONNECTED) {
          aborted = true;
          break;
        }
        bool self = ip == number(WiFi.localIP());
        char freshMac[18] = {};
        bool online = self || probeArp(ip, freshMac) || ping(ip) || probeTcp(ip);
        xSemaphoreTake(lock, portMAX_DELAY);
        int old = -1;
        for (unsigned j = 0; j < deviceCount; j++)
          if (devices[j].ip == ip) {
            old = j;
            break;
          }
        Device d;
        if (old >= 0)
          d = devices[old];
        xSemaphoreGive(lock);
        if (online || old >= 0) {
          d.ip = ip;
          d.changed = old >= 0 && d.online != online;
          d.isNew = old < 0;
          d.online = online;
          if (online) {
            d.seen = millis();
            if (self) {
              strlcpy(d.name, "networkmonitor", sizeof(d.name));
              strlcpy(d.mac, WiFi.macAddress().c_str(), sizeof(d.mac));
            } else {
              if (freshMac[0]) {
                if (strcasecmp(d.mac, freshMac)) {
                  d.name[0] = 0;
                  d.hostname[0] = 0;
                }
                strlcpy(d.mac, freshMac, sizeof(d.mac));
              }
            }
          }
          fresh[count++] = d;
        }
        xSemaphoreTake(lock, portMAX_DELAY);
        progress++;
        xSemaphoreGive(lock);
        vTaskDelay(1);
      }
    if (!aborted) {
      const IPAddress dns = WiFi.dnsIP();
      const uint32_t localIP = number(WiFi.localIP());
      for (unsigned i = 0; i < count; ++i) {
        if (WiFi.status() != WL_CONNECTED || wifiLinkGeneration.load() != generation) {
          aborted = true;
          break;
        }
        auto &d = fresh[i];
        if (!d.ip || !d.online)
          continue;
        if (d.ip == localIP) {
          strlcpy(d.hostname, "networkmonitor.local", sizeof(d.hostname));
        } else {
          String hostname = reverseName(d.ip, dns, 45);
          if (hostname.length())
            strlcpy(d.hostname, hostname.c_str(), sizeof(d.hostname));
        }
        if (scanSource == "local" && d.hostname[0] && d.ip != localIP)
          strlcpy(d.name, d.hostname, sizeof(d.name));
      }
    }
    Device *oldReport = nullptr;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (!aborted && (WiFi.status() != WL_CONNECTED || wifiLinkGeneration.load() != generation))
      aborted = true;
    if (!aborted) {
      oldReport = devices;
      devices = fresh;
      fresh = nullptr;
      deviceCount = count;
      lastScan = millis();
      lastScanEpoch = utcNow();
      scanNumber++;
      completed = true;
    } else if (scanError.isEmpty())
      scanError = "Wi-Fi disconnected. Previous report retained.";
    scanning = false;
    xSemaphoreGive(lock);
    delete[] oldReport;
    delete[] fresh;
    if (!aborted && scanSource == "glinet")
      Serial.printf("Router scan complete: %u clients, %u bytes of scanner stack free at low water.\n",
                    count, unsigned(uxTaskGetStackHighWaterMark(nullptr)));
    uint32_t elapsed = millis() - began;
    uint32_t interval = scanIntervalSeconds * 1000UL;
    const uint32_t wait = elapsed < interval ? interval - elapsed : 250;
    const uint32_t waitStarted = millis();
    while (millis() - waitStarted < wait) {
      if (WiFi.status() != WL_CONNECTED || wifiLinkGeneration.load() != generation) {
        closeRouterSSH();
        break;
      }
      uint32_t waited = millis() - waitStarted;
      if (waited >= wait || ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(min(wait - waited, 1000UL))))
        break;
    }
  }
}
const char STYLE[] PROGMEM =
    R"HTML(<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="color-scheme" content="light dark"><title>Network Monitor</title><script src="/assets/theme.js"></script><link rel="stylesheet" href="/assets/ui.css">)HTML";
void gzipAsset(const uint8_t *data, size_t length, const char *type) {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, type, reinterpret_cast<const char *>(data), length);
}
bool auth() {
  if (server.authenticate("admin", adminPassword.c_str()))
    return true;
  server.requestAuthentication(DIGEST_AUTH, "Network Monitor");
  return false;
}
void dashboard() { gzipAsset(DASHBOARD_HTML, sizeof(DASHBOARD_HTML), "text/html; charset=utf-8"); }
void forceScan() {
  if (server.arg("token") != scanToken) {
    server.send(403, "application/json",
                "{\"error\":\"Reload the dashboard before requesting a scan.\"}");
    return;
  }
  if (WiFi.status() != WL_CONNECTED || !scannerTask) {
    server.send(409, "application/json",
                "{\"error\":\"Scanner unavailable. Check the Wi-Fi connection.\"}");
    return;
  }
  static uint32_t lastForced = 0;
  xSemaphoreTake(lock, portMAX_DELAY);
  if (scanning || scanRequested) {
    xSemaphoreGive(lock);
    server.send(202, "application/json", "{\"status\":\"scanning\"}");
    return;
  }
  if (lastForced && millis() - lastForced < 10000) {
    xSemaphoreGive(lock);
    server.send(429, "application/json",
                "{\"error\":\"Please wait 10 seconds between manual scans.\"}");
    return;
  }
  lastForced = millis();
  scanRequested = true;
  xSemaphoreGive(lock);
  xTaskNotifyGive(scannerTask);
  server.send(202, "application/json", "{\"status\":\"requested\"}");
}
void api() {
  String out;
  xSemaphoreTake(lock, portMAX_DELAY);
  if (!out.reserve(1024)) {
    xSemaphoreGive(lock);
    server.send(503, "text/plain", "Monitor busy; retry shortly.");
    return;
  }
  unsigned onlineCount = 0, offlineCount = 0, newCount = 0, changedCount = 0;
  for (unsigned i = 0; i < deviceCount; i++) {
    if (devices[i].online)
      onlineCount++;
    else
      offlineCount++;
    if (devices[i].isNew)
      newCount++;
    if (devices[i].changed)
      changedCount++;
  }
  uint64_t now = utcNow();
  uint64_t scanned = lastScanEpoch;
  if (completed && !scanned && now)
    scanned = now - (millis() - lastScan) / 1000;
  out += "{\"firmware\":\"glinet-ui-9\",\"nowEpoch\":" + String(now) +
         ",\"uptimeSeconds\":" + String(uint64_t(esp_timer_get_time()) / 1000000ULL) +
         ",\"timeZone\":" + json(timeZone) + ",\"timeFormat\":" + json(timeFormat) +
         ",\"timeServer\":" + json(timeServer) + ",\"scanNumber\":" + String(scanNumber) +
         ",\"clockSynced\":" + (now ? "true" : "false") + ",\"lastScanEpoch\":" + String(scanned) +
         ",\"scanToken\":" + json(scanToken) +
         ",\"scanRequested\":" + (scanRequested ? "true" : "false") +
         ",\"clientMode\":" + String(scanSource == "glinet" ? "true" : "false") +
         ",\"source\":" + json(scanSource) + ",\"ssid\":" + json(ssid) +
         ",\"interval\":" + scanIntervalSeconds + ",\"online\":" + onlineCount +
         ",\"offline\":" + offlineCount + ",\"newDevices\":" + newCount +
         ",\"changedDevices\":" + changedCount +
         ",\"scanning\":" + String(scanning ? "true" : "false") +
         ",\"completed\":" + (completed ? "true" : "false") + ",\"progress\":" + progress +
         ",\"age\":" + ((millis() - lastScan) / 1000) + ",\"error\":" + json(scanError) +
         ",\"devices\":[";
  // Only the scanner replaces this table. Hold its lock while sending each
  // row so a response always contains the rows matching its summary.
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent(out);
  for (unsigned i = 0; i < deviceCount && server.client().connected(); i++) {
    out = i ? "," : "";
    const auto &d = devices[i];
    out += "{\"ip\":" + json(d.ip ? address(d.ip).toString() : String("")) +
           ",\"name\":" + json(d.name) + ",\"hostname\":" + json(d.hostname) +
           ",\"mac\":" + json(d.mac) +
           ",\"status\":" + json(d.online ? "ONLINE" : "OFFLINE") +
           ",\"changed\":" + (d.changed ? "true" : "false") +
           ",\"isNew\":" + (d.isNew ? "true" : "false") + ",\"connection\":" + json(d.connection) +
           "}";
    server.sendContent(out);
  }
  xSemaphoreGive(lock);
  server.sendContent("]}");
}
String field(String label, String name, String value, String type = "text") {
  return "<label class='field'>" + label + "<input name='" + name + "' type='" + type +
         "' value='" + escape(value) + "' autocomplete='" +
         (type == "password" ? "new-password" : "off") + "'></label>";
}
String timeSettingsFields() {
  String p =
      "<section class='card settings-card' id='time'><div class='section-title'><span "
      "class='section-index'>04</span><div><h2>Date &amp; time</h2><p>Choose the clock shown to "
      "everyone viewing the dashboard.</p></div></div><div class='form-grid'><label "
      "class='field'>Time zone<select name='timeZone' id='timeZone'><option selected value='" +
      escape(timeZone) + "'>" + escape(timeZone) +
      "</option></select></label><label class='field'>Time format<select name='timeFormat' "
      "id='timeFormat'>";
  for (auto format : {"24", "12"})
    p += "<option value='" + String(format) + "'" + (timeFormat == format ? " selected" : "") +
         ">" + String(format) + " hours</option>";
  p +=
      "</select></label><label class='field'>Time server<select name='timeServer' id='timeServer'>";
  bool preset = false;
  for (auto host : {"time.nist.gov", "pool.ntp.org", "time.cloudflare.com", "time.google.com"}) {
    bool selected = timeServer == host;
    preset = preset || selected;
    p += "<option value='" + String(host) + "'" + (selected ? " selected" : "") + ">" +
         String(host) + "</option>";
  }
  p += "<option value='custom'" + String(preset ? "" : " selected") +
       ">Custom server</option></select></label><div id='customTimeServerField'>" +
       field("Custom NTP hostname or IPv4 address", "customTimeServer",
             preset ? String("") : timeServer) +
       "</div></div><p class='helper' id='timePreview'></p><p class='helper'>24-hour time is the "
       "default. Time zones follow daylight-saving changes automatically. NTP Pool uses "
       "pool.ntp.org. NIST and NTP Pool include provider-specific fallback servers; other choices "
       "use the selected server only.</p></section>";
  return p;
}
#if defined(ARDUINO_AirM2M_CORE_ESP32C3) || defined(RGB_BUILTIN)
void updateActivityLed() {
  // D4 is active high. Keep short scans visible for at least three flashes.
  const uint32_t now = millis();
  static uint32_t activitySince = 0;
  static bool activity = false;
  if (scanning.load()) {
    if (!activity)
      activitySince = now;
    activity = true;
  } else if (now - activitySince >= 750) {
    activity = false;
  }
  const bool on = activityLedEnabled &&
                  (activity ? (now - activitySince) % 250 < 125 : now % 2000 < 125);
#if defined(RGB_BUILTIN)
  const uint32_t color = on ? activityLedColor : 0;
  static uint32_t lastColor = UINT32_MAX;
  if (color != lastColor) {
    rgbLedWrite(RGB_BUILTIN, color >> 16, color >> 8, color);
    lastColor = color;
  }
#else
  digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
#endif
}
#endif

#if defined(ARDUINO_AirM2M_CORE_ESP32C3) || defined(RGB_BUILTIN)
void saveActivityLed() {
  if (!auth())
    return;
  if (server.arg("csrf") != csrf) {
    server.send(403, "text/plain", "Invalid form token. Reload Admin.");
    return;
  }
  const String enabled = server.arg("activityLed");
  if (enabled != "0" && enabled != "1") {
    server.send(400, "text/plain", "Choose On or Off for the activity light.");
    return;
  }
#if defined(RGB_BUILTIN)
  const String colorText = server.arg("ledColor");
  bool validColor = colorText.length() == 7 && colorText[0] == '#';
  for (unsigned i = 1; validColor && i < 7; ++i)
    validColor = isxdigit(static_cast<unsigned char>(colorText[i]));
  if (!validColor) {
    server.send(400, "text/plain", "Choose a valid LED color.");
    return;
  }
  const uint32_t color = strtoul(colorText.c_str() + 1, nullptr, 16);
  // Pack RGB and the enable flag into one write so both settings save together.
  if (prefs.putUInt("activityLed", color | (enabled == "1" ? 0x1000000 : 0)) != sizeof(uint32_t)) {
#else
  if (prefs.putBool("activityLed", enabled == "1") != sizeof(uint8_t)) {
#endif
    server.send(500, "text/plain", "Could not save the activity light settings.");
    return;
  }
#if defined(RGB_BUILTIN)
  activityLedColor = color;
#endif
  activityLedEnabled = enabled == "1";
  updateActivityLed();
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Location", "/admin#activity-light");
  server.send(303, "text/plain", "");
}
#endif
void restartSystem() {
  if (!auth())
    return;
  if (server.arg("csrf") != csrf) {
    server.send(403, "text/plain", "Invalid form token. Reload Admin.");
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html; charset=utf-8",
              String(STYLE) +
                  "<main class='shell'><section class='card settings-card'><h1>Restarting system"
                  "</h1><p>Saved settings are kept. Reopen the dashboard after the system "
                  "reconnects.</p><a class='btn btn-primary' href='/'>Open dashboard</a>"
                  "</section></main>");
  restartAt = millis() + 1500;
}
void factoryReset() {
  if (!auth())
    return;
  if (server.arg("csrf") != csrf) {
    server.send(403, "text/plain", "Invalid form token. Reload Admin.");
    return;
  }
  if (server.arg("confirmation") != "RESET") {
    server.send(400, "text/plain", "Type RESET to confirm a factory reset.");
    return;
  }
  // Perform the wipe at boot, before Wi-Fi/scanner tasks can access NVS.
  if (!prefs.putBool("resetPending", true)) {
    server.send(500, "text/plain", "Could not schedule factory reset. No settings were erased.");
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.send(
      200, "text/html; charset=utf-8",
      String(STYLE) +
          "<main class='shell'><section class='card settings-card'><h1>Factory reset "
          "scheduled</h1><p>The monitor is restarting and clearing all saved settings. Connect to "
          "<strong>NetworkMonitor-Setup</strong> using <strong>netmon123</strong>, then open <a "
          "href='http://192.168.4.1/admin'>192.168.4.1/admin</a>.</p><p>Admin username: "
          "<strong>admin</strong><br>Admin password: "
          "<strong>netmonadmin123</strong></p></section></"
          "main><script>try{localStorage.removeItem('nm-theme');localStorage.removeItem('nm-font-"
          "size')}catch(e){}</script>");
  restartAt = millis() + 1500;
}
void admin() {
  if (!auth())
    return;
  String p;
  p.reserve(17000);
  p +=
      String(STYLE) +
      R"HTML(<header class="topbar"><div class="shell topbar-inner"><a class="brand" href="/"><span class="brand-mark"><svg viewBox="0 0 24 24" aria-hidden="true"><rect x="3" y="3" width="6" height="6" rx="1.5"/><rect x="15" y="15" width="6" height="6" rx="1.5"/><path d="M6 9v9h9M9 6h9v9"/></svg></span><div><span class="eyebrow">Administration</span><h1>Network Monitor</h1></div></a><div class="actions"><div class="font-controls" role="group" aria-label="Page text size"><button class="btn" type="button" data-font-step="-1" aria-label="Decrease text size">A-</button><button class="btn font-reset" type="button" data-font-reset aria-label="Reset text size to 100 percent"><span data-font-label aria-live="polite">100%</span></button><button class="btn" type="button" data-font-step="1" aria-label="Increase text size">A+</button></div><button class="btn" type="button" data-theme-toggle><svg class="theme-moon" viewBox="0 0 24 24" aria-hidden="true"><path d="M20.5 14A8.5 8.5 0 0 1 10 3.5 8.5 8.5 0 1 0 20.5 14Z"/></svg><svg class="theme-sun" viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="4"/><path d="M12 2v2m0 16v2M2 12h2m16 0h2"/></svg><span class="theme-label" data-theme-label>Dark mode</span></button><a class="btn btn-primary" href="/">Back to dashboard</a></div></div></header><main class="shell"><div class="admin-layout"><nav class="settings-nav" aria-label="Settings sections"><div class="nav-label">Configuration</div><a href="#wifi">Wi-Fi connection</a><a href="#scan">Scan preferences</a><a href="#router">Router connection</a><a href="#time">Date &amp; time</a><a href="#security">Admin security</a>)HTML";
#if defined(ARDUINO_AirM2M_CORE_ESP32C3) || defined(RGB_BUILTIN)
  p += "<a href='#activity-light'>Activity light</a>";
#endif
  p += R"HTML(<a href="#restart">Restart system</a><a href="#factory">Factory reset</a></nav><div><div class="form-title"><span class="eyebrow">Your monitor, your settings</span><h2>Monitor settings</h2><p>Changes are saved to the system and applied after a restart.</p></div><form method="post" action="/admin" id="settingsForm"><input type="hidden" name="csrf" value=")HTML";
  p +=
      csrf +
      R"HTML("><section class='card settings-card' id='wifi'><div class='section-title'><span class='section-index'>01</span><div><h2>Wi-Fi connection</h2><p>Connect the monitor to your local wireless network.</p></div></div><div class='form-grid'>)HTML";
  p += field("Network name (SSID)", "ssid", ssid) + field("Wi-Fi password", "wifi", "", "password");
  p += "<label class='field'>Wi-Fi channel<select name='wifiChannel'><option value='0'>"
       "Automatic</option>";
  for (unsigned channel = 1; channel <= 13; ++channel) {
    p += "<option value='" + String(channel) + "'" +
         (channel == wifiChannel ? " selected" : "") + ">" + channel + " (" +
         (2407 + 5 * channel) + " MHz)</option>";
  }
  p += "</select></label><label class='field'>Channel width<select name='wifiWidth'>";
  for (unsigned width : {20U, 40U}) {
    p += "<option value='" + String(width) + "'" + (width == wifiWidth ? " selected" : "") +
         ">" + width + " MHz</option>";
  }
  p += "</select></label></div>";
  if (WiFi.status() == WL_CONNECTED) {
    uint8_t primary = 0;
    wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
    p += "<p class='helper'>Connected on channel " + String(WiFi.channel());
    if (esp_wifi_get_channel(&primary, &secondary) == ESP_OK)
      p += secondary == WIFI_SECOND_CHAN_NONE ? " at 20 MHz" : " at 40 MHz";
    p += " (" + String(WiFi.RSSI()) + " dBm).</p>";
  }
  p += "<p class='helper'>A channel selection tells the system where to look first. "
       "40 MHz uses 20 MHz when the access point requires it.</p>"
       "<p class='helper'>Leave the password blank to keep the saved "
       "Wi-Fi password.</p><label class='check'><input type='checkbox' "
       "name='open' value='yes'>Use an open Wi-Fi network (no "
       "password)</label></section><section class='card settings-card' "
       "id='scan'><div class='section-title'><span "
       "class='section-index'>02</span><div><h2>Scan preferences</h2><p>Choose "
       "where to look for devices and how often to update.</p></div></div><div "
       "class='form-grid'><label class='field'>Discovery source<select "
       "name='source' id='source'>";
  p += "<option value='local'" + String(scanSource == "local" ? " selected" : "") +
       ">Local ARP / ping / TCP</option><option value='openwrt'" +
       String(scanSource == "openwrt" ? " selected" : "") +
       ">OpenWrt HTTP (legacy)</option><option value='glinet'" +
       String(scanSource == "glinet" ? " selected" : "") +
       ">GL.iNet live client list</option></select></label><label class='field'>Scan "
       "interval (seconds)<input type='number' name='interval' min='10' "
       "max='86400' step='1' required value='" +
       String(scanIntervalSeconds) +
       "'></label><label class='field'>Address selection<select name='mode' "
       "id='mode'>";
  for (auto m : {"auto", "subnet", "range"})
    p += "<option value='" + String(m) + "'" + (mode == m ? " selected" : "") + ">" +
         String(!strcmp(m, "auto")     ? "Automatic Wi-Fi subnet"
                : !strcmp(m, "subnet") ? "CIDR subnet"
                                       : "Specific IP range") +
         "</option>";
  p += "</select></label></div><div id='subnetFields'>" +
       field("Subnet (CIDR), for example 192.168.2.0/24", "subnet", subnet) +
       "</div><div class='form-grid' id='rangeFields'>" +
       field("First IP address", "start", rangeStart) + field("Last IP address", "end", rangeEnd) +
       "</div><p class='helper'>Up to 254 addresses per range. Use 60 seconds "
       "for one-minute updates. GL.iNet mode imports the complete router client list; subnet/range "
       "filters apply only to other sources. The dashboard's Scan now button requests an "
       "additional scan.</p></section><section class='card settings-card' "
       "id='router'><div class='section-title'><span "
       "class='section-index'>03</span><div><h2>Router connection</h2><p>Use "
       "your existing OpenWrt login to retrieve device "
       "records.</p></div></div><p class='helper' id='localSourceNote'>These "
       "settings are retained when local discovery is selected.</p><div "
       "class='form-grid'><label class='field'>Connection type<select "
       "name='transport' id='transport'><option value='http'" +
       String(routerTransport == "http" ? " selected" : "") +
       ">HTTP / ubus</option><option value='ssh'" +
       String(routerTransport == "ssh" ? " selected" : "") + ">SSH</option></select></label>" +
       field("Router IPv4 address", "routerIP", routerIP) +
       field("Port (SSH 22, HTTP 80)", "routerPort", String(routerPort), "number") +
       field("User ID", "routerUser", routerUser) +
       field("Router password", "routerPassword", "", "password") +
       "</div><p class='helper'>Leave the password blank to keep it. GL.iNet mode uses SSH to read "
       "the router's own clients.get_list API on loopback. Its online flag, aliases and "
       "disconnected-client records supply the report. No neighbor or DHCP status inference is "
       "used. Legacy OpenWrt HTTP needs authenticated /ubus access.</p><details "
       "class='identity'><summary>Saved SSH router identity</summary><code>";
  Preferences identity;
  String saved = "Not learned yet";
  if (identity.begin("netmon-ssh", true)) {
    saved = identity.getString("endpoint", "") + " " + identity.getString("key", "");
    identity.end();
  }
  p += escape(saved) +
       "</code><label class='check'><input type='checkbox' name='forgetSSH' "
       "value='yes'>Forget this identity after verifying a replacement "
       "router</label></details></section>" +
       timeSettingsFields() +
       "<section class='card settings-card' "
       "id='security'><div class='section-title'><span "
       "class='section-index'>05</span><div><h2>Admin security</h2><p>Update "
       "the password used to sign in to this settings "
       "page.</p></div></div><div class='form-grid'>" +
       field("New admin password", "admin", "", "password") +
       field("Confirm new password", "confirm", "", "password") +
       "</div><p class='helper'>Use at least 12 characters. Leave both fields "
       "blank to keep the current password. Your username is "
       "admin.</p></section><div class='card savebar'><p>Saving restarts the "
       "system. Your current device history will reset.</p><button "
       "type='submit' class='btn btn-primary' id='saveButton'>Save settings "
       "&amp; restart</button></div></form>";
#if defined(ARDUINO_AirM2M_CORE_ESP32C3) || defined(RGB_BUILTIN)
  p += "<section class='card settings-card' id='activity-light'><div class='section-title'>"
       "<div><h2>Activity light</h2><p>Show a slow heartbeat between scans and rapid flashes "
       "during a scan.</p></div></div><form method='post' action='/admin/activity-light'>"
       "<input type='hidden' name='csrf' value='" + csrf +
       "'><div class='form-grid'><label class='field'>Activity light<select name='activityLed'>"
       "<option value='1'" + String(activityLedEnabled ? " selected" : "") +
       ">On</option><option value='0'" + String(activityLedEnabled ? "" : " selected") +
       ">Off</option></select></label>";
#if defined(RGB_BUILTIN)
  char colorValue[8];
  snprintf(colorValue, sizeof(colorValue), "#%06lx", static_cast<unsigned long>(activityLedColor));
  p += field("LED color", "ledColor", colorValue, "color");
#endif
  p += "</div><p class='helper'>Applies immediately.</p>"
       "<button class='btn' type='submit'>Save light settings</button></form></section>";
#endif
  p += "<section class='card settings-card' id='restart'><div class='section-title'>"
       "<span class='section-index'>06</span><div><h2>Restart system</h2>"
       "<p>Reboot using the saved settings. The dashboard will briefly disconnect.</p>"
       "</div></div><form method='post' action='/admin/restart'>"
       "<input type='hidden' name='csrf' value='" + csrf +
       "'><button class='btn' type='submit'>Restart system</button></form></section>";
  p += "<section class='card settings-card danger-section' id='factory'><div "
       "class='section-title'><span class='section-index'>07</span><div><h2>Factory "
       "reset</h2><p>Erase this monitor's settings and start again.</p></div></div><p "
       "class='helper'>This clears Wi-Fi and router credentials, admin password, scan/time "
       "settings, activity light settings, saved SSH identity and device history. You will need to reconnect through the "
       "setup hotspot.</p><p class='helper'>After reset: setup Wi-Fi password "
       "<strong>netmon123</strong>; admin username <strong>admin</strong>, password "
       "<strong>netmonadmin123</strong>.</p><form method='post' action='/admin/factory-reset' "
       "id='resetForm'><input type='hidden' name='csrf' value='" +
       csrf +
       "'><label class='field'>Type RESET to erase all settings<input name='confirmation' "
       "id='resetConfirmation' required pattern='RESET' autocomplete='off' "
       "placeholder='RESET'></label><button class='btn btn-danger' type='submit' id='resetButton' "
       "disabled>Erase everything &amp; reset</button></form></section></div></div><footer "
       "class='page-footer'><span>Network Monitor / Administration</span><span>Time server: " +
       escape(timeServer) + "</span></footer></main><script defer src='/assets/admin.js'></script>";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html; charset=utf-8", p);
}
void save() {
  if (!auth())
    return;
  if (server.arg("csrf") != csrf) {
    server.send(403, "text/plain", "Invalid form token. Reload Admin.");
    return;
  }
  String s = server.arg("ssid"), w = server.arg("wifi"), a = server.arg("admin"),
         m = server.arg("mode");
  String channelText = server.hasArg("wifiChannel") ? server.arg("wifiChannel") : String(wifiChannel);
  long channel = channelText.toInt();
  String width = server.hasArg("wifiWidth") ? server.arg("wifiWidth") : String(wifiWidth);
  if (channel < 0 || channel > 13 || String(channel) != channelText ||
      (width != "20" && width != "40")) {
    server.send(400, "text/plain",
                "Choose Automatic or Wi-Fi channel 1 to 13 and a width of 20 or 40 MHz.");
    return;
  }
  String intervalText = server.arg("interval");
  bool validInterval = intervalText.length() > 0 && intervalText.length() <= 5;
  for (unsigned i = 0; i < intervalText.length(); i++)
    if (intervalText[i] < '0' || intervalText[i] > '9')
      validInterval = false;
  uint32_t interval = intervalText.toInt();
  String source = server.arg("source"), rip = server.arg("routerIP"),
         user = server.arg("routerUser"), password = server.arg("routerPassword"),
         portText = server.arg("routerPort");
  String transport = source == "glinet" ? "ssh" : "http";
  uint32_t routerAddress = 0;
  long port = portText.toInt();
  bool validRouter =
      (portText.length() > 0 && portText.length() <= 5 && port >= 1 && port <= 65535);
  for (unsigned i = 0; i < portText.length(); i++)
    if (portText[i] < '0' || portText[i] > '9')
      validRouter = false;
  if ((source != "local" && source != "openwrt" && source != "glinet") ||
      (transport != "http" && transport != "ssh") || password.length() > 128 ||
      user.length() > 64 ||
      (source != "local" && (!owIP(rip.c_str(), routerAddress) || !user.length() ||
                             !(password.length() || routerPassword.length()) || !validRouter))) {
    server.send(400, "text/plain",
                "Check scan source, OpenWrt connection, IPv4 address, port, user ID "
                "and password.");
    return;
  }
  String zone = server.arg("timeZone"), format = server.arg("timeFormat");
  String ntp = server.arg("timeServer");
  if (ntp == "custom")
    ntp = server.arg("customTimeServer");
  ntp.trim();
  if (!validTimeZone(zone.c_str()) || (format != "12" && format != "24") ||
      !validTimeServer(ntp.c_str())) {
    server.send(400, "text/plain",
                "Choose a valid time zone, 12/24-hour format and NTP hostname or IPv4 address "
                "(without a URL or port).");
    return;
  }
  uint32_t lo, hi;
  if (!validInterval || interval < 10 || interval > 86400 || s.length() < 1 || s.length() > 32 ||
      w.length() > 63 || (w.length() > 0 && w.length() < 8) || (a.length() && a.length() < 12) ||
      a.length() > 128 || a != server.arg("confirm") ||
      (m != "auto" &&
       !bounds(m, server.arg("subnet"), server.arg("start"), server.arg("end"), lo, hi))) {
    server.send(400, "text/plain",
                "Invalid settings. Check SSID, password lengths, confirmation, "
                "scan interval (10 to 86400 seconds), and scan range (maximum "
                "254 IPs; CIDR /24 through /30).");
    return;
  }
  if (prefs.putUChar("wifiChannel", channel) != sizeof(uint8_t) ||
      prefs.putUChar("wifiWidth", width.toInt()) != sizeof(uint8_t)) {
    server.send(500, "text/plain", "Could not save Wi-Fi connection settings.");
    return;
  }
  if (server.arg("forgetSSH") == "yes")
    prefs.putBool("forgetSSH", true);
  prefs.putString("transport", transport);
  prefs.putString("source", source);
  prefs.putString("routerIP", rip);
  prefs.putString("routerUser", user);
  if (password.length())
    prefs.putString("routerPwd", password);
  if (validRouter)
    prefs.putUShort("routerPort", port);
  prefs.putString("timeZone", zone);
  prefs.putString("timeFormat", format);
  prefs.putString("timeServer", ntp);
  prefs.putUInt("interval", interval);
  prefs.putString("ssid", s);
  if (server.arg("open") == "yes")
    prefs.putString("wifi", "");
  else if (w.length())
    prefs.putString("wifi", w);
  if (a.length())
    prefs.putString("admin", a);
  prefs.putString("mode", m);
  prefs.putString("subnet", server.arg("subnet"));
  prefs.putString("start", server.arg("start"));
  prefs.putString("end", server.arg("end"));
  server.send(200, "text/html; charset=utf-8",
              String(STYLE) + "<main><h1>Settings saved</h1><p>Restarting. Reconnect to your Wi-Fi "
                              "and open <a "
                              "href='http://networkmonitor.local'>networkmonitor.local</a>. If "
                              "Wi-Fi fails, reconnect to the setup network.</p></main>");
  restartAt = millis() + 1500;
}
void printConnectionInfo() {
  Serial.println("\nNetwork Monitor (glinet-ui-9) | Serial: 115200 baud | "
                 "send ? to repeat");
  Serial.printf("Admin username: admin\nAdmin password: %s\nSetup Wi-Fi password: %s\n",
                adminPassword.c_str(), setupPassword.c_str());
  if (apRunning)
    Serial.printf("Setup Wi-Fi: NetworkMonitor-Setup\nAdmin: http://%s/admin\n",
                  WiFi.softAPIP().toString().c_str());
  if (WiFi.status() == WL_CONNECTED)
    Serial.printf("Dashboard: http://%s/\n", WiFi.localIP().toString().c_str());
}
void setupAP() {
  if (apRunning)
    return;
  static uint32_t lastAttempt = 0;
  if (lastAttempt && millis() - lastAttempt < 5000)
    return;
  lastAttempt = millis();
  if (!WiFi.mode(WIFI_AP_STA) || !WiFi.softAP("NetworkMonitor-Setup", setupPassword.c_str())) {
    Serial.println("Setup hotspot failed to start; retrying in 5 seconds.");
    return;
  }
  apRunning = true;
  printConnectionInfo();
}
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.printf("\nNetwork Monitor (glinet-ui-9) starting; reset reason: %d\n",
                int(esp_reset_reason()));
  prefs.begin("netmonitor", false);
  if (prefs.getBool("resetPending", false)) {
    prefs.end();
    esp_err_t rc = nvs_flash_erase();
    if (rc != ESP_OK) {
      Serial.printf("Factory reset could not erase NVS (%d). Restart to retry.\n", int(rc));
      while (true)
        delay(1000);
    }
    ESP.restart();
  }
#if defined(RGB_BUILTIN)
  const uint32_t ledSettings = prefs.getUInt("activityLed", 0x1000000 | DEFAULT_LED_COLOR);
  activityLedEnabled = (ledSettings & 0x1000000) != 0;
  activityLedColor = ledSettings & 0xffffff;
  updateActivityLed();
#elif defined(ARDUINO_AirM2M_CORE_ESP32C3)
  activityLedEnabled = prefs.getBool("activityLed", true);
  pinMode(LED_BUILTIN, OUTPUT);
  updateActivityLed();
#endif
  ssid = prefs.getString("ssid", "");
  wifiPassword = prefs.getString("wifi", "");
  wifiChannel = prefs.getUChar("wifiChannel", 0);
  if (wifiChannel > 13)
    wifiChannel = 0;
  wifiWidth = prefs.getUChar("wifiWidth", 40) == 20 ? 20 : 40;
  adminPassword = prefs.getString("admin", "");
  if (!adminPassword.length()) {
    adminPassword = DEFAULT_ADMIN_PASSWORD;
    prefs.putString("admin", adminPassword);
  }
  setupPassword = prefs.getString("setup", "");
  if (!setupPassword.length()) {
    setupPassword = DEFAULT_AP_PASSWORD;
    prefs.putString("setup", setupPassword);
  }
  Serial.printf("Admin username: admin\nAdmin password: %s\nSetup Wi-Fi password: %s\n",
                adminPassword.c_str(), setupPassword.c_str());
  csrf = String(esp_random(), HEX) + String(esp_random(), HEX);
  scanToken = String(esp_random(), HEX) + String(esp_random(), HEX);
  scanSource = prefs.getString("source", "local");
  if (scanSource != "openwrt" && scanSource != "glinet")
    scanSource = "local";
  routerIP = prefs.getString("routerIP", "192.168.1.1");
  routerTransport = prefs.getString("transport", "http");
  if (routerTransport != "ssh")
    routerTransport = "http";
  routerPort = prefs.getUShort("routerPort", 80);
  if (scanSource == "openwrt" && routerTransport == "ssh") {
    scanSource = "glinet";
    prefs.putString("source", scanSource);
  }
  if (scanSource == "glinet")
    routerTransport = "ssh";
  routerUser = prefs.getString("routerUser", "root");
  routerPassword = prefs.getString("routerPwd", "");
  scanIntervalSeconds = prefs.getUInt("interval", 60);
  if (scanIntervalSeconds < 10 || scanIntervalSeconds > 86400)
    scanIntervalSeconds = 60;
  timeZone = prefs.getString("timeZone", DEFAULT_TIME_ZONE);
  if (!validTimeZone(timeZone.c_str()))
    timeZone = DEFAULT_TIME_ZONE;
  timeFormat = prefs.getString("timeFormat", "24");
  if (timeFormat != "12" && timeFormat != "24")
    timeFormat = "24";
  timeServer = prefs.getString("timeServer", DEFAULT_TIME_SERVER);
  if (!validTimeServer(timeServer.c_str()))
    timeServer = DEFAULT_TIME_SERVER;
  mode = prefs.getString("mode", "auto");
  subnet = prefs.getString("subnet", "192.168.1.0/24");
  rangeStart = prefs.getString("start", "192.168.1.1");
  rangeEnd = prefs.getString("end", "192.168.1.254");
  lock = xSemaphoreCreateMutex();
  if (!lock) {
    Serial.println("Cannot allocate mutex");
    while (true)
      delay(1000);
  }
  WiFi.onEvent([](arduino_event_id_t) { wifiLinkGeneration.fetch_add(1); },
               ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.onEvent([](arduino_event_id_t) { wifiLinkGeneration.fetch_add(1); },
               ARDUINO_EVENT_WIFI_STA_LOST_IP);
  WiFi.setHostname("networkmonitor");
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  if (ssid.length()) {
    if (!WiFi.STA.bandwidth(wifiWidth == 20 ? WIFI_BW_HT20 : WIFI_BW_HT40))
      Serial.println("Could not set the Wi-Fi channel width. Connecting with the driver default.");
    WiFi.begin(ssid.c_str(), wifiPassword.c_str(), wifiChannel);
  } else
    setupAP();
  server.on("/", HTTP_GET, dashboard);
  server.on("/api/devices", HTTP_GET, api);
  server.on("/api/scan", HTTP_POST, forceScan);
  server.on("/assets/ui.css", HTTP_GET,
            []() { gzipAsset(UI_CSS, sizeof(UI_CSS), "text/css; charset=utf-8"); });
  server.on("/assets/theme.js", HTTP_GET,
            []() { gzipAsset(THEME_JS, sizeof(THEME_JS), "text/javascript; charset=utf-8"); });
  server.on("/assets/admin.js", HTTP_GET,
            []() { gzipAsset(ADMIN_JS, sizeof(ADMIN_JS), "text/javascript; charset=utf-8"); });
  server.on("/assets/timezones.json", HTTP_GET,
            []() { gzipAsset(TIMEZONES_JSON, sizeof(TIMEZONES_JSON), "application/json"); });
  server.on("/assets/dashboard.js", HTTP_GET, []() {
    gzipAsset(DASHBOARD_JS, sizeof(DASHBOARD_JS), "text/javascript; charset=utf-8");
  });
  server.on("/admin", HTTP_GET, admin);
  server.on("/admin", HTTP_POST, save);
#if defined(ARDUINO_AirM2M_CORE_ESP32C3) || defined(RGB_BUILTIN)
  server.on("/admin/activity-light", HTTP_POST, saveActivityLed);
#endif
  server.on("/admin/restart", HTTP_POST, restartSystem);
  server.on("/admin/factory-reset", HTTP_POST, factoryReset);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();
  if (prefs.getBool("forgetSSH", false)) {
    Preferences trust;
    if (trust.begin("netmon-ssh", false)) {
      if (trust.clear())
        prefs.putBool("forgetSSH", false);
      trust.end();
    }
  }
  if (scanSource == "glinet")
    libssh_begin();
  unsigned stackSize = scanSource == "glinet" ? 32768 : 24576;
  if (xTaskCreate(scanTask, "network-scan", stackSize, nullptr, 1, &scannerTask) != pdPASS)
    scanError = "Unable to start scanner: restart the monitor.";
}
void loop() {
  server.handleClient();
#if defined(ARDUINO_AirM2M_CORE_ESP32C3) || defined(RGB_BUILTIN)
  updateActivityLed();
#endif
  while (Serial.available()) {
    if (Serial.read() == '?')
      printConnectionInfo();
  }
  if (restartAt && int32_t(millis() - restartAt) >= 0)
    ESP.restart();
  static bool advertised = false;
  static bool clockStarted = false;
  static uint32_t disconnectedAt = 0;
  if (WiFi.status() == WL_CONNECTED) {
    if (!clockStarted) {
      esp_sntp_set_time_sync_notification_cb(clockSyncCallback);
      if (timeServer == "time.nist.gov")
        configTime(0, 0, timeServer.c_str(), "time-a-g.nist.gov", "time-b-g.nist.gov");
      else if (timeServer == "pool.ntp.org")
        configTime(0, 0, timeServer.c_str(), "0.pool.ntp.org", "1.pool.ntp.org");
      else
        configTime(0, 0, timeServer.c_str());
      esp_sntp_set_sync_interval(3600000);
      clockStarted = true;
    }
    disconnectedAt = 0;
    if (!advertised) {
      advertised = MDNS.begin("networkmonitor");
      if (advertised)
        MDNS.addService("http", "tcp", 80);
      Serial.printf("Dashboard: http://%s/ or http://networkmonitor.local/\n",
                    WiFi.localIP().toString().c_str());
      if (WiFi.getMode() == WIFI_AP_STA) {
        WiFi.softAPdisconnect(true);
        apRunning = false;
        WiFi.mode(WIFI_STA);
      }
    }
  } else {
    if (advertised) {
      MDNS.end();
      advertised = false;
    }
    if (!disconnectedAt)
      disconnectedAt = millis();
    if (millis() - disconnectedAt > 30000)
      setupAP();
  }
  delay(2);
}
