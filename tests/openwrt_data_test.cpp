#include "../NetworkMonitor/OpenWrtData.h"
#include <cassert>
#include <iostream>
cJSON *parse(const char *s) {
  auto *v = cJSON_Parse(s);
  assert(v);
  return v;
}
int main() {
  uint32_t ip;
  assert(owIP("192.168.1.2", ip) && ip == 0xc0a80102);
  for (auto s :
       {"192.168.1.256", "+192.168.1.1", "192.168.1.1junk", "192.168.1", "0.1.2.3", "224.1.2.3"})
    assert(!owIP(s, ip));
  DeviceTable rows;
  RouterSnapshot snapshot = {rows, 0xc0a80101, 0xc0a801fe};
  auto *h = parse(
      R"({"aa:bb:cc:dd:ee:01":{"ipaddrs":["192.168.1.2","2001:db8::1"],"name":"Laptop"},"aa:bb:cc:dd:ee:02":{"ipaddrs":["192.168.1.3"],"name":"Printer"},"aa:bb:cc:dd:ee:04":{"ipaddrs":["10.0.0.2"]}})");
  assert(snapshot.hints(h));
  cJSON_Delete(h);
  assert(snapshot.rows.size() == 2 && !rows[0].online && !rows[1].online);
  assert(rows[0].isNew && !strcmp(rows[0].mac, "AA:BB:CC:DD:EE:01"));
  h = parse(
      R"({"dhcp_leases":[{"ipaddr":"192.168.1.4","macaddr":"aa:bb:cc:dd:ee:03","hostname":"Phone","expires":86400}]})");
  assert(snapshot.leases(h));
  cJSON_Delete(h);
  assert(snapshot.rows.size() == 3 && !rows[2].online); // valid lease alone never online
  assert(
      snapshot.neighbors("192.168.1.2 dev br-lan lladdr aa:bb:cc:dd:ee:01 REACHABLE\n192.168.1.3 "
                         "dev br-lan lladdr aa:bb:cc:dd:ee:02 STALE\n"));
  assert(rows[0].online && !rows[1].online);
  h = parse(
      R"({"clients":{"AA:BB:CC:DD:EE:03":{"assoc":true,"authorized":true},"aa:bb:cc:dd:ee:02":{"assoc":false,"authorized":false}}})");
  assert(snapshot.clients(h));
  cJSON_Delete(h);
  assert(rows[2].online && !rows[1].online);
  assert(snapshot.neighbors(""));
  assert(!snapshot.neighbors("invalid neighbor output\n"));
  h = parse("{}");
  assert(!snapshot.leases(h) && !snapshot.clients(h));
  cJSON_Delete(h);
  assert(snapshot.host(0xc0a80105));
  const Device *first = &rows[0];
  assert(snapshot.host(0xc0a80106)); // crosses a block boundary
  assert(&rows[0] == first && rows[0].online);

  DeviceTable capped;
  RouterSnapshot all = {capped, 1, MAX_HOSTS + 1};
  for (unsigned i = 1; i <= MAX_HOSTS; ++i)
    assert(all.host(i));
  assert(!all.host(MAX_HOSTS + 1) && all.overflow && !all.allocationFailed);

  // Discarding a partial report leaves the published report intact.
  DeviceTable scratch;
  assert(scratch.append(rows[0]));
  RouterSnapshot next = {scratch, 0xc0a80101, 0xc0a801fe};
  scratch[0].online = false;
  assert(next.neighbors("192.168.1.2 dev br-lan lladdr aa:bb:cc:dd:ee:01 FAILED\n"));
  assert(!scratch[0].online && rows[0].online);
  scratch.clear(1);
  assert(scratch.size() == 0 && scratch.append() && !scratch[0].mac[0]);
  rows.swap(scratch);
  assert(rows.size() == 1 && scratch.size() == 5 && scratch[0].online);
  scratch.clear(rows.size());
  assert(scratch.size() == 0 && scratch.append() && !scratch[0].online);
  std::cout << "OpenWrt data checks passed\n";
}
