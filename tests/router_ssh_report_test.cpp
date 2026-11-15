#include "../NetworkMonitor/RouterSSHReport.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
void output(RouterSSHReport &r, const std::string &s) {
  for (size_t i = 0; i < s.size();) {
    size_t n = std::min(size_t(7), s.size() - i);
    r.data(s.data() + i, n, false);
    i += n;
  }
}
int main() {
  DeviceTable rows;
  RouterSnapshot snapshot = {rows, 0xc0a80101, 0xc0a801fe};
  RouterSSHReport good(snapshot);
  output(good, R"(NM2_GLCLIENTS
{"mac":"aa:bb:cc:dd:ee:01","ip":"192.168.1.2","name":"laptop","online":true,"connection":"5G"}
NM2_DONE 1
)");
  assert(!good.terminal() && !good.valid());
  good.eof = true;
  assert(!good.terminal() && !good.valid());
  good.exitReceived = true;
  good.exitStatus = 0;
  assert(good.terminal() && good.valid());
  assert(!strcmp(good.parser.phase(), "complete") && snapshot.rows[0].online);
  RouterSSHReport reordered(snapshot); // empty output is valid only on successful exit
  output(reordered, "NM2_GLCLIENTS\nNM2_DONE 0\n");
  reordered.exitReceived = true;
  reordered.exitStatus = 0;
  for (int idle = 0; idle < 100; idle++)
    assert(!reordered.terminal());
  reordered.closed = true;
  assert(reordered.valid());
  RouterSSHReport truncated(snapshot);
  output(truncated, "NM2_GLCLIENTS\n{");
  truncated.eof = true;
  truncated.exitReceived = true;
  truncated.exitStatus = 0;
  assert(!truncated.valid());
  RouterSSHReport failed(snapshot);
  failed.eof = true;
  failed.exitReceived = true;
  failed.exitStatus = 1;
  const char *message = "/sbin/ip: not found\n";
  failed.data(message, strlen(message), true);
  assert(!failed.valid());
  assert(strstr(failed.stderrText, "not found"));
  RouterSSHReport large(snapshot);
  output(large, "NM2_GLCLIENTS\n");
  for (unsigned i = 0; i < 1500; i++)
    output(large, "{\"mac\":\"aa:bb:cc:dd:ee:02\",\"ip\":\"192.168.1.3\",\"name\":\"named-"
                  "offline\",\"online\":false,\"connection\":\"cable\"}\n");
  output(large, "NM2_DONE 1500\n");
  large.eof = true;
  large.exitReceived = true;
  large.exitStatus = 0;
  assert(large.valid() && snapshot.rows.size() == 2 && !strcmp(snapshot.rows[1].name.c_str(), "named-offline") &&
         !snapshot.rows[1].online);
  assert(large.bytes > 32768);
  RouterSSHReport bounded(snapshot);
  std::string errors(5000, 'x');
  bounded.data(errors.data(), errors.size(), true);
  assert(bounded.failure && bounded.stderrUsed == 160 && strlen(bounded.stderrText) == 160);
  RouterSSHReport limit(snapshot);
  limit.bytes = 2097152;
  limit.data("x", 1, false);
  assert(limit.failure);
  std::cout << "GL.iNet SSH callback checks passed: streamed large output, "
               "EOF/exit order, idle periods, truncation, empty output/command failures "
               "and bounded errors.\n";
}
