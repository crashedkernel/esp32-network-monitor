#include "../NetworkMonitor/TimeSettings.h"
#include <cassert>
#include <iostream>
#include <string>
int main() {
  assert(validTimeZone(DEFAULT_TIME_ZONE) && validTimeZone("UTC") &&
         validTimeZone("Asia/Kuala_Lumpur"));
  assert(!validTimeZone("Mars/Olympus_Mons") && !validTimeZone("America/New_York<script>"));
  for (auto server : {"time.nist.gov", "pool.ntp.org", "0.pool.ntp.org", "time.cloudflare.com",
                      "192.168.2.1", "router"})
    assert(validTimeServer(server));
  for (auto server : {"", "https://time.nist.gov", "pool.ntp.org:123", "-host.test", "host-.test",
                      "host..test", "host.test.", "host test", "host/test", "x;reboot"})
    assert(!validTimeServer(server));
  assert(!validTimeServer((std::string(64, 'a') + ".test").c_str()));
  assert(!strcmp(DEFAULT_AP_PASSWORD, "netmon123") &&
         !strcmp(DEFAULT_ADMIN_PASSWORD, "netmonadmin123"));
  std::cout << "Time settings checks passed: configured zones, NTP host validation and factory "
               "password defaults.\n";
}
