# Router client-list sources

In `/admin`, choose **GL.iNet live client list**, enter the router IPv4 address, SSH port (normally **22**), user (normally **root**) and its saved password. Blank passwords preserve saved credentials. The ESP32 admin password is separate. Existing OpenWrt/SSH settings migrate to GL.iNet mode on upgrade. Local ARP / ping / TCP and OpenWrt HTTP (legacy) remain available as separate choices.

## GL.iNet client API over SSH

GL.iNet documents this local API request in its [support example](https://forum.gl-inet.com/t/i-need-to-log-and-save-to-nas-the-user-traffic-on-my-x3000/56867):

```sh
curl -k http://127.0.0.1/rpc -H 'glinet: 1' \
  -d '{"jsonrpc":"2.0","method":"call","id":1,"params":["","clients","get_list",{}]}'
```

This is the vendor's client inventory API, containing names/aliases, IP/MAC addresses, connection interfaces and online flags. See also GL.iNet's [client-list guide](https://docs.gl-inet.com/router/en/4/interface_guide/clients/). The ESP32 runs this fixed read-only request inside the authenticated SSH session; it accepts local HTTP-to-HTTPS redirects and the router's loopback self-signed certificate. The SSH host-key check protects the connection from the ESP32 to the router. No router settings, clients or traffic statistics are changed. No credentials appear in shell command text or diagnostics.

The router needs its GL.iNet 4.x local client API, `curl`, `lua` or `luajit`, and a JSON module (`cjson.safe`, `cjson` or `luci.jsonc`). These worked on the connected router without installing or changing any router software. The Lua reducer runs on the router and removes traffic histories before streaming compact records to the ESP32; the board never buffers the full API response. Missing helpers/API access produce an error and retain the previous report. No neighbor/lease/probe fallback occurs.

### Status and identity

- **ONLINE / OFFLINE** use the API's explicit `online` flag, including the router's disconnected-client history.
- Names prefer GL.iNet's configured alias, then its name/hostname. Connection comes from its interface field.
- Clients are keyed by **MAC**. Changing IPv4 address does not create a new client. Multiple MACs can share the same last-known IP.
- Offline clients without a current IPv4 address remain listed; a previous IP for that MAC is retained when known, otherwise shown as Unknown.
- Each successful report follows the complete GL.iNet list. Removed router entries are removed here too. Subnet/range filters apply only to other scan sources.
- Missing/invalid online flags and failed/truncated reports retain the last report. A successful empty list is a valid empty inventory.

This follows the router's own tracking freshness and offline-history settings. It does not independently prove physical power state. The former SSH `ip neigh show` command/parser and DHCP name enrichment have been removed from this source.

### Setup and limits

Use Arduino-ESP32 3.3.12, LibSSH-ESP32 5.10.0 and **Huge APP (3MB No OTA/1MB SPIFFS)**. A normal upload preserves NVS settings. Keep scan interval **60 seconds** for one-minute polls; **Scan now** requests an extra pass. The page fetches data every five seconds.

SSH verifies and stores the router's key after the first successful login. Later connections verify the saved key before password authentication. The endpoint/fingerprint is visible in Admin. If the router/key/address is intentionally replaced, verify it before using **Forget saved SSH identity**. Root password changes alone do not require forgetting the key.

Limits: **254 stored clients**, **1535 bytes per reduced record**, **2 MB transferred output**. Each report requires the completion count, EOF/close and successful command exit before publication. SSH read budget is 50 seconds; connect/auth/channel timeout is five seconds. Working/published tables are sized for the inventory and swapped atomically. Router API timeout is 20 seconds. No complete API-response String/JSON tree is held on the ESP32.

## Board uptime

The dashboard adds **Board uptime** after **Last completed scan**. It uses `esp_timer_get_time()` (64-bit monotonic time since this ESP32 boot), advances every second, and works without NTP. It is not the router's uptime. A board restart resets it.

## HTTP router requirements

HTTP uses authenticated JSON-RPC at `/ubus` on port 80 by default. It is not SSH, HTTPS or the legacy `/cgi-bin/luci/rpc` endpoint. HTTP transmits the router password without transport encryption, so use a trusted LAN. Redirects are rejected. A router that redirects HTTP to HTTPS needs a suitable LAN HTTP endpoint or should use the SSH option instead.

Both modes store the router password in ESP32 Preferences without flash encryption. It is never included in the public dashboard API, populated into the admin form, or printed to Serial.


The router needs `uhttpd-mod-ubus`, `rpcd`, `rpcd-mod-luci`, `rpcd-mod-file`, `/sbin/ip` with IPv4 neighbor support, and authenticated `/ubus` access from the ESP32. These may already be installed with LuCI. Verify the following on the router over SSH:

```sh
ubus -v list luci-rpc
ubus -v list file
ubus call luci-rpc getHostHints
ubus call luci-rpc getDHCPLeases '{"family":4}'
/sbin/ip -4 neigh show
ubus call network.wireless status
```

On an opkg-based release, missing packages can be installed with:

```sh
opkg update
opkg install uhttpd-mod-ubus rpcd rpcd-mod-luci rpcd-mod-file ip-tiny
```

On an apk-based release, use that release's package manager to install the same available packages instead. Check installed packages first; do not replace the router's existing full `ip` package with `ip-tiny`.

Verify the uhttpd instance serving the LAN HTTP port has `option ubus_prefix '/ubus'` and authentication is enabled (`ubus_noauth` unset or `0`). Keep the endpoint restricted to your trusted LAN; no WAN access is required. Restart uhttpd/rpcd after changing their configuration. Some vendor images omit required RPC objects; the ESP32 reports the failing method instead of silently falling back to incomplete data.

## HTTP: use your existing root login

No separate `networkmonitor` user is required. In the ESP32 admin form choose **OpenWrt router**, set **OpenWrt user ID** to **root**, and enter your router's existing root password in **OpenWrt password**. This is the router password, not the ESP32 admin password. The sketch already sends these saved values to `session.login`; root is the default router user on fresh settings.

OpenWrt normally uses this existing `/etc/config/rpcd` login to authenticate against the root password in `/etc/shadow`:

```uci
config login
        option username 'root'
        option password '$p$root'
        list read '*'
        list write '*'
```

If it already exists, leave it in place; do not add a duplicate. `$p$root` is a reference to the system root password, not a placeholder for a hash or literal password. See [OpenWrt HTTP ubus authentication](https://openwrt.org/docs/techref/ubus).

The commands succeeding over SSH confirm the router methods are installed, but HTTP RPC still checks session permissions. Copy the included `openwrt-http-acl.json` to `/usr/share/rpcd/acl.d/networkmonitor.json` and restart rpcd if needed to grant the exact neighbor-read command and other reads. The normal root login's wildcard ACL groups include this role automatically; no additional login section is needed. In particular, `file.exec` checks the command ACL even for an authenticated root RPC session. Keep `/ubus` authentication enabled.


## HTTP: optional restricted rpcd login

Copy `openwrt-http-acl.json` from this directory to `/usr/share/rpcd/acl.d/networkmonitor.json` on the router. It allows only the required read operations and the exact neighbor-display command, not arbitrary shell commands or configuration writes.

Create an rpcd login in `/etc/config/rpcd`, using a password hash generated on the router with `uhttpd -m` (run it with your chosen password as its argument). The following is a template; replace the password placeholder with the actual generated hash:

```uci
config login
        option username 'networkmonitor'
        option password 'PASTE_GENERATED_PASSWORD_HASH_HERE'
        list read 'networkmonitor'
```

The hash belongs in the router config; enter the original password in the ESP32 form. No Linux shell account is required for this rpcd-only login. Restart rpcd after installing the ACL and login, then configure that user in the ESP32. Retain an existing administrator login while testing.

The `file.exec` call runs `/sbin/ip` directly with the fixed argument array `-4`, `neigh`, `show`. User-entered values are never substituted into a shell command. There are no router configuration changes or active client probes during a router scan. Login/logout manage only the monitor's own RPC session.

## What is considered online in HTTP mode

The monitor merges these router records within the configured IPv4 subnet/range:

- `luci-rpc.getHostHints`: names and IP/MAC mappings derived from neighbor data, static DHCP configuration, ethers and local interfaces.
- `luci-rpc.getDHCPLeases`: known IPv4 DHCP leases and names, including devices that are no longer active.
- `/sbin/ip -4 neigh show`: **REACHABLE** neighbor entries provide current reachability evidence. STALE, DELAY, PROBE, FAILED, INCOMPLETE, PERMANENT and other states do not by themselves prove current presence.
- `network.wireless.status` and `hostapd.<interface>.get_clients`: currently associated **and authorized** clients are Online when their MAC maps to a known IPv4 address.
- The router's own IP is Online after a successful authenticated scan.

A lease or static reservation alone never makes a host Online. **OFFLINE in router mode means no current confirmation from the router**, rather than proof that the device is powered off. Idle wired devices may have STALE neighbor entries and appear OFFLINE even while connected. HTTP router mode deliberately performs no ping, ARP refresh or TCP probes. Use the local scanner if active verification is needed.

Wi-Fi associations cover access points on the queried router, not independent external APs. A MAC with no known IPv4 mapping cannot be listed in this IPv4 table. Cached IP mappings can lag DHCP changes; one MAC with multiple known IPv4 addresses may occupy multiple rows. Devices behind another router/NAT or outside the selected range are not automatically enumerated.

All known router candidates can appear, including offline leases/reservations never previously seen by the ESP32. Such rows count as **new** in the scan where they are first imported. Previous rows are retained and re-evaluated on every successful snapshot. History is keyed by IPv4 address and is cleared after restarting the ESP32.

The report is committed only after all required reads succeed. Authentication errors, SSH identity changes, missing permissions/packages, invalid responses, Wi-Fi loss, or oversized results leave the previous report and its counters unchanged and display an error. No automatic switch to local probes occurs. A fresh installation with an error keeps an empty report until the first successful scan.

HTTP limits: 254 IPv4 addresses in the selected subnet/range, 254 stored rows, 16 AP interfaces and 32 KB per raw HTTP response. GL.iNet client-output limits are documented above. HTTP uses 3-second connect and 5-second read timeouts; GL.iNet SSH timeout details are above. Subnet/range filtering happens on the ESP32; it cannot reduce the size of the router's raw RPC response. A very large router inventory may exceed the applicable response limit even with a small selected range. This raw-response limitation applies to HTTP; GL.iNet mode streams reduced API records without buffering the full response.

## Verification on your router

1. Verify login and one successful report, then compare IP/MAC/name data with LuCI.
2. Confirm an associated device that blocks ping is Online; disconnect it and check that a lease alone does not keep it Online.
3. Compare a wired device's status to `ip -4 neigh show`; remember that STALE is not confirmed online.
4. Enter an incorrect password, then restore it: the error must retain the previous report instead of marking every host offline.
5. Change source back to Local ARP / ping / TCP and confirm the original scanner still works.

References: [OpenWrt ubus HTTP authentication](https://openwrt.org/docs/techref/ubus), [LuCI host hints and leases implementation](https://github.com/openwrt/luci/blob/master/libs/rpcd-mod-luci/src/luci.c), [hostapd client API](https://github.com/openwrt/openwrt/blob/main/package/network/services/hostapd/README.md), [rpcd file API and command ACL checks](https://github.com/openwrt/rpcd/blob/master/file.c).

SSH implementation: [LibSSH-ESP32](https://github.com/ewpa/LibSSH-ESP32), version 5.10.0. The sketch compiles with Arduino-ESP32 3.3.12 and Huge APP partitioning; the SSH reader has been exercised on the connected board/router.

## Validation

`glinet-ui-5` compiled for AirM2M_CORE_ESP32C3 with Huge APP: **1,340,155 bytes flash**, **41,188 bytes global RAM**. Tests cover explicit client flags, IP/MAC identity, names, missing/shared IPs, empty/truncated reports and SSH exit ordering. The Lua reducer tests exercise array/MAC-keyed API responses, aliases, Unicode, module fallbacks and traffic-history removal. Dashboard checks include live uptime and restart values.

Live verification on the connected router returned **106 ONLINE / 7 OFFLINE**, **111 names / 113 clients**, from `clients.get_list`. A forced scan completed successfully and ESP32 uptime advanced independently. Admin, NTP/time settings, static assets and factory-reset guards passed. Saved credentials were retained; factory reset was not executed.

The dashboard/Admin retain light/dark mode, 80–150% text size, search, status filters, configurable timezone/12–24-hour time/NTP server and confirmed factory reset. Fresh/factory-reset credentials remain setup AP **netmon123**, Admin **admin / netmonadmin123**. Current saved passwords survive uploads.
