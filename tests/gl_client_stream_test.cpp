#include "../NetworkMonitor/GLClientStream.h"
#include <cassert>
#include <iostream>
#include <string>
void feed(GLClientStream &r, const std::string &s) {
  for (char c : s)
    assert(r.feed(c));
}
int main() {
  DeviceTable rows;
  RouterSnapshot snap = {rows, 0xc0a80101, 0xc0a801fe};
  GLClientStream r(snap);
  feed(r,
       "NM2_GLCLIENTS\n{\"mac\":\"aa:bb:cc:dd:ee:01\",\"ip\":\"192.168.1.2\",\"name\":\"My "
       "laptop\",\"hostname\":\"laptop.home.arpa\",\"online\":true,\"connection\":\"5G\"}\n{\"mac\":\"aa:bb:cc:dd:ee:02\",\"ip\":"
       "\"192.168.1.2\",\"name\":\"Old "
       "phone\",\"online\":false,\"connection\":\"cable\"}\n{\"mac\":\"aa:bb:cc:dd:ee:03\",\"ip\":"
       "\"\",\"name\":\"Disconnected tablet\",\"online\":false,\"connection\":\"\"}\nNM2_DONE 3\n");
  assert(r.finish() && snap.rows.size() == 3 && rows[0].online && !rows[1].online && !rows[2].online);
  assert(rows[0].ip == rows[1].ip && !rows[2].ip && !strcmp(rows[0].name.c_str(), "My laptop"));
  assert(!strcmp(rows[0].hostname.c_str(), "laptop.home.arpa") && rows[1].hostname.empty());
  assert(!strcmp(rows[0].connection.c_str(), "5G"));
  // Identity follows MAC across address changes; two MACs may share a last IP.
  Device *same = snap.client("AA:BB:CC:DD:EE:01", 0xc0a80155);
  assert(same == &rows[0] && snap.rows.size() == 3 && same->ip == 0xc0a80155);
  for (const std::string data :
       {"", "NM2_GLCLIENTS\n", "NM2_GLCLIENTS\nNM2_DONE 1\n",
        "NM2_GLCLIENTS\n{\"mac\":\"aa:bb:cc:dd:ee:01\",\"ip\":\"\",\"name\":\"x\",\"connection\":"
        "\"wifi\"}\nNM2_DONE 1\n",
        "NM2_GLCLIENTS\n{\"mac\":\"aa:bb:cc:dd:ee:01\",\"ip\":\"\",\"name\":\"x\",\"online\":"
        "\"false\",\"connection\":\"wifi\"}\nNM2_DONE 1\n"}) {
    DeviceTable work;
    RouterSnapshot s = {work, 0, 0xffffffff};
    GLClientStream broken(s);
    bool failed = false;
    for (char c : data)
      if (!broken.feed(c)) {
        failed = true;
        break;
      }
    assert(failed || !broken.finish());
  }
  DeviceTable emptyRows;
  RouterSnapshot empty = {emptyRows, 0, 0xffffffff};
  GLClientStream noRows(empty);
  feed(noRows, "NM2_GLCLIENTS\nNM2_DONE 0\n");
  assert(noRows.finish() && empty.rows.size() == 0);
  DeviceTable published, pending;
  for (unsigned scan = 0; scan < 2; ++scan) {
    RouterSnapshot inventory = {pending, 0, 0xffffffff};
    GLClientStream stream(inventory);
    feed(stream, "NM2_GLCLIENTS\n");
    for (unsigned i = 0; i < MAX_HOSTS; ++i) {
      char mac[18];
      snprintf(mac, sizeof(mac), "aa:bb:cc:dd:ee:%02x", i);
      const auto id = std::to_string(i);
      feed(stream, "{\"mac\":\"" + std::string(mac) + "\",\"ip\":\"192.168.1." + id +
                       "\",\"name\":\"Device " + id + "\",\"hostname\":\"host-" + id +
                       ".lan\",\"online\":" + (scan ? "false" : "true") +
                       ",\"connection\":\"5G\"}\n");
    }
    feed(stream, "NM2_DONE " + std::to_string(MAX_HOSTS) + "\n");
    assert(stream.finish() && pending.size() == 255);
    if (scan) {
      assert(published.size() == 255 && published[254].online && !pending[254].online);
      assert(!strcmp(pending[254].hostname.c_str(), "host-254.lan"));
      pending[254].hostname = published[254].hostname;
    }
    published.swap(pending);
    pending.clear(published.size());
    assert(!strcmp(published[254].hostname.c_str(), "host-254.lan"));
  }
  std::cout << "GL.iNet client checks passed: explicit online flags, names, MAC identity, "
               "duplicate/missing IPs, empty lists, truncation guards and two 255-device reports.\n";
}
