#pragma once
#include "OpenWrtData.h"
#include "OpenWrtSSH.h"
#include <HTTPClient.h>

// HTTPClient decodes chunked bodies into this capped sink.
class RouterBody : public Stream {
public:
  String body;
  bool full = false;
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t *p, size_t n) override {
    if (body.length() + n > 32768) {
      full = true;
      return 0;
    }
    size_t before = body.length();
    body.concat((const char *)p, n);
    return body.length() - before;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
};
// Returns a detached result object owned by caller. Never logs credentials.
cJSON *routerCall(const String &session, const char *object, const char *method, const String &args,
                  String &error) {
  NetworkClient client;
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(5000);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(client, "http://" + routerIP + ":" + String(routerPort) + "/ubus")) {
    error = "Cannot open OpenWrt connection.";
    return nullptr;
  }
  http.addHeader("Content-Type", "application/json");
  String request = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"call\",\"params\":[" +
                   json(session) + "," + json(object) + "," + json(method) + "," + args + "]}";
  int status = http.POST(request);
  RouterBody response;
  if (status != 200) {
    http.end();
    error = "OpenWrt HTTP error " + String(status) + ". Check router IP, port and /ubus access.";
    return nullptr;
  }
  if (http.getSize() > 32768) {
    http.end();
    error = "OpenWrt response exceeds 32 KB.";
    return nullptr;
  }
  int read = http.writeToStream(&response);
  http.end();
  if (read < 0 || response.full) {
    error = "OpenWrt response incomplete or larger than 32 KB.";
    return nullptr;
  }
  cJSON *root = cJSON_Parse(response.body.c_str());
  const cJSON *id = owItem(root, "id");
  cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
  const cJSON *code = cJSON_GetArrayItem(result, 0);
  if (!root || !cJSON_IsNumber(id) || id->valueint != 1 || !cJSON_IsArray(result) ||
      !cJSON_IsNumber(code) || code->valueint != 0) {
    error = "OpenWrt RPC failed: " + String(object) + "." + method +
            " (check packages, login and permissions).";
    cJSON_Delete(root);
    return nullptr;
  }
  cJSON *data = cJSON_DetachItemFromArray(result, 1);
  cJSON_Delete(root);
  if (!cJSON_IsObject(data)) {
    cJSON_Delete(data);
    error = "Invalid OpenWrt RPC response.";
    return nullptr;
  }
  return data;
}
// The scanner owns this token. OpenWrt refreshes its idle timeout on use.
String openWrtSession;
uint32_t openWrtLastUse = 0;
bool readRouterSnapshot(RouterSnapshot &snapshot, String &error) {
  if (scanSource == "glinet") {
    if (!openRouterSSH(error))
      return false;
    bool ok = readRouterSSHClients(snapshot, error);
    if (!ok)
      closeRouterSSH();
    return ok;
  }
  cJSON *data = nullptr;
  if (openWrtSession.isEmpty() || millis() - openWrtLastUse >= 175000) {
    data = routerCall("00000000000000000000000000000000", "session", "login",
                      "{\"username\":" + json(routerUser) +
                          ",\"password\":" + json(routerPassword) + ",\"timeout\":180}",
                      error);
    if (!data)
      return false;
    openWrtSession = owText(owItem(data, "ubus_rpc_session"));
    cJSON_Delete(data);
    if (openWrtSession.length() != 32) {
      openWrtSession = "";
      error = "OpenWrt login did not return a session.";
      return false;
    }
  }
  bool ok = false;
  do {
    data = routerCall(openWrtSession, "luci-rpc", "getHostHints", "{}", error);
    if (!data)
      break;
    bool valid = snapshot.hints(data);
    cJSON_Delete(data);
    if (!valid) {
      error = "Invalid or oversized OpenWrt host hints (maximum 254 devices).";
      break;
    }
    data = routerCall(openWrtSession, "luci-rpc", "getDHCPLeases", "{\"family\":4}", error);
    if (!data)
      break;
    valid = snapshot.leases(data);
    cJSON_Delete(data);
    if (!valid) {
      error = "Invalid or oversized OpenWrt DHCP lease list.";
      break;
    }
    // Read only the current IPv4 neighbor table. No shell interpolation.
    data = routerCall(openWrtSession, "file", "exec",
                      "{\"command\":\"/sbin/ip\",\"params\":[\"-4\",\"neigh\",\"show\"]}", error);
    if (!data)
      break;
    const cJSON *code = owItem(data, "code"), *output = owItem(data, "stdout");
    valid = cJSON_IsNumber(code) && code->valueint == 0 && (!output || cJSON_IsString(output)) &&
            snapshot.neighbors(owText(output));
    cJSON_Delete(data);
    if (!valid) {
      error = "OpenWrt neighbor query failed. Check /sbin/ip and file.exec "
              "permissions.";
      break;
    }
    data = routerCall(openWrtSession, "network.wireless", "status", "{}", error);
    if (!data)
      break;
    // Collect interface names before freeing the response to bound peak RAM.
    String interfaces[16];
    unsigned count = 0;
    valid = true;
    for (const cJSON *radio = data->child; radio && valid; radio = radio->next) {
      if (!cJSON_IsObject(radio)) {
        valid = false;
        break;
      }
      if (cJSON_IsFalse(owItem(radio, "up")))
        continue;
      const cJSON *list = owItem(radio, "interfaces");
      if (!cJSON_IsArray(list)) {
        valid = false;
        break;
      }
      for (const cJSON *iface = list->child; iface; iface = iface->next) {
        const cJSON *config = owItem(iface, "config");
        String ifname = owText(owItem(iface, "ifname"));
        if (strcmp(owText(owItem(config, "mode")), "ap") || !ifname.length())
          continue;
        if (count == 16) {
          valid = false;
          break;
        }
        interfaces[count++] = ifname;
      }
    }
    cJSON_Delete(data);
    if (!valid) {
      error = "Invalid OpenWrt wireless status or more than 16 access points.";
      break;
    }
    for (unsigned i = 0; i < count; i++) {
      String object = "hostapd." + interfaces[i];
      data = routerCall(openWrtSession, object.c_str(), "get_clients", "{}", error);
      if (!data) {
        valid = false;
        break;
      }
      valid = snapshot.clients(data);
      cJSON_Delete(data);
      if (!valid) {
        error = "Invalid OpenWrt Wi-Fi client list.";
        break;
      }
    }
    if (!valid)
      break;
    // A successful authenticated query is direct evidence the router is online.
    uint32_t ip;
    if (owIP(routerIP.c_str(), ip)) {
      Device *router = snapshot.host(ip);
      if (router) {
        router->online = true;
        if (!router->name[0])
          owCopy(router->name, "OpenWrt router", sizeof(router->name));
      }
    }
    ok = !snapshot.overflow;
    if (!ok)
      error = "OpenWrt inventory exceeds 254 devices.";
  } while (false);
  if (ok)
    openWrtLastUse = millis();
  else
    openWrtSession = "";
  return ok;
}
