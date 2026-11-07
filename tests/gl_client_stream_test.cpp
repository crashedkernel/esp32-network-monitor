#include "../NetworkMonitor/GLClientStream.h"
#include <cassert>
#include <iostream>
#include <string>
void feed(GLClientStream &r, const std::string &s) {
  for (char c : s)
    assert(r.feed(c));
}
int main() {
  Device rows[8];
  RouterSnapshot snap = {rows, 0, 8, 0xc0a80101, 0xc0a801fe};
  GLClientStream r(snap);
  feed(r,
       "NM2_GLCLIENTS\n{\"mac\":\"aa:bb:cc:dd:ee:01\",\"ip\":\"192.168.1.2\",\"name\":\"My "
       "laptop\",\"online\":true,\"connection\":\"5G\"}\n{\"mac\":\"aa:bb:cc:dd:ee:02\",\"ip\":"
       "\"192.168.1.2\",\"name\":\"Old "
       "phone\",\"online\":false,\"connection\":\"cable\"}\n{\"mac\":\"aa:bb:cc:dd:ee:03\",\"ip\":"
       "\"\",\"name\":\"Disconnected tablet\",\"online\":false,\"connection\":\"\"}\nNM2_DONE 3\n");
  assert(r.finish() && snap.count == 3 && rows[0].online && !rows[1].online && !rows[2].online);
  assert(rows[0].ip == rows[1].ip && !rows[2].ip && !strcmp(rows[0].name, "My laptop"));
  assert(!strcmp(rows[0].connection, "5G"));
  // Identity follows MAC across address changes; two MACs may share a last IP.
  Device *same = snap.client("AA:BB:CC:DD:EE:01", 0xc0a80155);
  assert(same == &rows[0] && snap.count == 3 && same->ip == 0xc0a80155);
  for (const std::string data :
       {"", "NM2_GLCLIENTS\n", "NM2_GLCLIENTS\nNM2_DONE 1\n",
        "NM2_GLCLIENTS\n{\"mac\":\"aa:bb:cc:dd:ee:01\",\"ip\":\"\",\"name\":\"x\",\"connection\":"
        "\"wifi\"}\nNM2_DONE 1\n",
        "NM2_GLCLIENTS\n{\"mac\":\"aa:bb:cc:dd:ee:01\",\"ip\":\"\",\"name\":\"x\",\"online\":"
        "\"false\",\"connection\":\"wifi\"}\nNM2_DONE 1\n"}) {
    Device work[8];
    RouterSnapshot s = {work, 0, 8, 0, 0xffffffff};
    GLClientStream broken(s);
    bool failed = false;
    for (char c : data)
      if (!broken.feed(c)) {
        failed = true;
        break;
      }
    assert(failed || !broken.finish());
  }
  Device emptyRows[1];
  RouterSnapshot empty = {emptyRows, 0, 1, 0, 0xffffffff};
  GLClientStream noRows(empty);
  feed(noRows, "NM2_GLCLIENTS\nNM2_DONE 0\n");
  assert(noRows.finish() && empty.count == 0);
  std::cout << "GL.iNet client checks passed: explicit online flags, names, MAC identity, "
               "duplicate/missing IPs, empty lists and truncation guards.\n";
}
