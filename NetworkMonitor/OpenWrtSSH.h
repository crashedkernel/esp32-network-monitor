#pragma once
#include "OpenWrtData.h"
#include "RouterSSHReport.h"
#include "RouterCommand.h"
#include <errno.h>
#include <esp_heap_caps.h>
#include <libssh/callbacks.h>
#include <libssh/libssh.h>
#include <libssh_esp32.h>

ssh_session routerSSH = nullptr;
void closeRouterSSH() {
  if (routerSSH) {
    ssh_disconnect(routerSSH);
    ssh_free(routerSSH);
    routerSSH = nullptr;
  }
}
bool openRouterSSH(String &error) {
  closeRouterSSH();
  routerSSH = ssh_new();
  if (!routerSSH) {
    error = "Not enough memory to start SSH.";
    return false;
  }
  unsigned int port = routerPort;
  long timeout = 5;
  bool options = ssh_options_set(routerSSH, SSH_OPTIONS_HOST, routerIP.c_str()) == SSH_OK &&
                 ssh_options_set(routerSSH, SSH_OPTIONS_PORT, &port) == SSH_OK &&
                 ssh_options_set(routerSSH, SSH_OPTIONS_USER, routerUser.c_str()) == SSH_OK &&
                 ssh_options_set(routerSSH, SSH_OPTIONS_TIMEOUT, &timeout) == SSH_OK;
  if (!options || ssh_connect(routerSSH) != SSH_OK) {
    error = "SSH connection failed. Check router address, SSH port and "
            "Dropbear service.";
    closeRouterSSH();
    return false;
  }
  ssh_key key = nullptr;
  unsigned char *hash = nullptr;
  size_t length = 0;
  int rc = ssh_get_server_publickey(routerSSH, &key);
  if (rc == SSH_OK)
    rc = ssh_get_publickey_hash(key, SSH_PUBLICKEY_HASH_SHA256, &hash, &length);
  if (key)
    ssh_key_free(key);
  char *text =
      rc == SSH_OK ? ssh_get_fingerprint_hash(SSH_PUBLICKEY_HASH_SHA256, hash, length) : nullptr;
  String fingerprint = text ? String(text) : String();
  if (text)
    ssh_string_free_char(text);
  if (hash)
    ssh_clean_pubkey_hash(&hash);
  if (!fingerprint.length()) {
    error = "Unable to verify SSH host key.";
    closeRouterSSH();
    return false;
  }
  Preferences trust;
  if (!trust.begin("netmon-ssh", false)) {
    error = "Cannot open saved SSH identity.";
    closeRouterSSH();
    return false;
  }
  String endpoint = routerIP + ":" + String(routerPort);
  String saved = trust.getString("key", "");
  if (saved.length() && (saved != fingerprint || trust.getString("endpoint", "") != endpoint)) {
    trust.end();
    error = "SSH router identity changed. Verify the router, then use Forget "
            "saved SSH identity in Admin.";
    closeRouterSSH();
    return false;
  }
  // Trust on first successful login; later connections check the key before
  // auth.
  if (ssh_userauth_password(routerSSH, nullptr, routerPassword.c_str()) != SSH_AUTH_SUCCESS) {
    trust.end();
    error = "SSH login failed. Check the router user/password and password "
            "login settings.";
    closeRouterSSH();
    return false;
  }
  if (!saved.length()) {
    bool stored =
        trust.putString("endpoint", endpoint) > 0 && trust.putString("key", fingerprint) > 0;
    if (!stored) {
      trust.end();
      error = "Could not save SSH router identity.";
      closeRouterSSH();
      return false;
    }
  }
  trust.end();
  return true;
}
int routerSSHData(ssh_session, ssh_channel, void *buffer, uint32_t length, int isStderr,
                  void *arg) {
  static_cast<RouterSSHReport *>(arg)->data(static_cast<const char *>(buffer), length,
                                            isStderr != 0);
  // Even after an error, consume the bytes so libssh never buffers the report.
  return int(length);
}
void routerSSHEof(ssh_session, ssh_channel, void *arg) {
  static_cast<RouterSSHReport *>(arg)->eof = true;
}
void routerSSHClosed(ssh_session, ssh_channel, void *arg) {
  static_cast<RouterSSHReport *>(arg)->closed = true;
}
void routerSSHExit(ssh_session, ssh_channel, int status, void *arg) {
  auto *report = static_cast<RouterSSHReport *>(arg);
  report->exitReceived = true;
  report->exitStatus = status;
}
String routerSSHDiagnostic(const char *stage, const RouterSSHReport &report, uint32_t start,
                           int savedErrno = 0) {
  String result = "SSH " + String(stage) + " [phase " + report.parser.phase() + ", " +
                  String((millis() - start) / 1000) + "s, " + report.bytes + " bytes, heap " +
                  ESP.getFreeHeap() + ", largest " +
                  heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) + "]";
  const char *detail = ssh_get_error(routerSSH);
  if (detail && *detail)
    result += ": " + String(detail).substring(0, 160);
  if (savedErrno)
    result += " (errno " + String(savedErrno) + ")";
  if (report.stderrUsed)
    result += "; router: " + String(report.stderrText);
  return result;
}
bool readRouterSSHClients(RouterSnapshot &snapshot, String &error) {
  RouterSSHReport report(snapshot);
  ssh_channel channel = ssh_channel_new(routerSSH);
  if (!channel) {
    error = "Not enough memory for SSH client-list channel.";
    return false;
  }
  ssh_channel_callbacks_struct callbacks = {};
  callbacks.userdata = &report;
  callbacks.channel_data_function = routerSSHData;
  callbacks.channel_eof_function = routerSSHEof;
  callbacks.channel_close_function = routerSSHClosed;
  callbacks.channel_exit_status_function = routerSSHExit;
  ssh_callbacks_init(&callbacks);
  ssh_event event = nullptr;
  bool registered = false, attached = false, ok = false;
  uint32_t start = millis();
  do {
    if (ssh_set_channel_callbacks(channel, &callbacks) != SSH_OK) {
      error = routerSSHDiagnostic("callback setup failed", report, start);
      break;
    }
    registered = true;
    if (ssh_channel_open_session(channel) != SSH_OK) {
      error = routerSSHDiagnostic("channel open failed", report, start);
      break;
    }
    if (ssh_channel_request_exec(channel, ROUTER_INVENTORY_COMMAND) != SSH_OK) {
      error = routerSSHDiagnostic("command start failed", report, start);
      break;
    }
    // The client command does not read SSH stdin.
    if (ssh_channel_send_eof(channel) != SSH_OK) {
      error = routerSSHDiagnostic("stdin close failed", report, start);
      break;
    }
    event = ssh_event_new();
    if (!event || ssh_event_add_session(event, routerSSH) != SSH_OK) {
      error = routerSSHDiagnostic("event setup failed", report, start);
      break;
    }
    attached = true;
    // Events continue to deliver exit-status after EOF. No repeated reads from
    // an already closed stderr stream, and normal idle timeouts are harmless.
    while (true) {
      if (report.failure) {
        error = "Router report: " + String(report.failure);
        break;
      }
      if (report.terminal()) {
        if (report.exitStatus != 0) {
          error = "GL.iNet clients.get_list exited " + String(report.exitStatus);
          if (report.stderrUsed)
            error += ": " + String(report.stderrText);
        } else if (!report.valid())
          error = String("Incomplete router report during ") + report.parser.phase() + ": " +
                  report.parser.error();
        else
          ok = true;
        break;
      }
      if (WiFi.status() != WL_CONNECTED) {
        error = "Wi-Fi disconnected during router scan.";
        break;
      }
      if (millis() - start > 50000) {
        error = routerSSHDiagnostic("scan exceeded 50-second budget", report, start);
        break;
      }
      if (report.closed) {
        error = routerSSHDiagnostic("channel closed without exit status", report, start);
        break;
      }
      if (!ssh_is_connected(routerSSH)) {
        error = routerSSHDiagnostic("transport disconnected", report, start);
        break;
      }
      errno = 0;
      int rc = ssh_event_dopoll(event, 100);
      int savedErrno = errno;
      if (rc == SSH_ERROR && !report.terminal()) {
        error = routerSSHDiagnostic("event read failed", report, start, savedErrno);
        break;
      }
      // SSH_AGAIN means a quiet connection, not a failed scan.
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  } while (false);
  if (attached)
    ssh_event_remove_session(event, routerSSH);
  if (event)
    ssh_event_free(event);
  // Capture errors before cleanup can replace the library's last error string.
  ssh_channel_close(channel);
  if (registered)
    ssh_remove_channel_callbacks(channel, &callbacks);
  ssh_channel_free(channel);
  return ok;
}
