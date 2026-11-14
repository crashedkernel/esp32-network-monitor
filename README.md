# ESP32 Network Monitor

A standalone network dashboard for the **LuatOS AirM2M CORE ESP32-C3**. Connect it to Wi-Fi, configure your router, and open the dashboard from any browser on your LAN.

The [latest tagged release is v1.0.0](https://github.com/crashedkernel/esp32-network-monitor/releases/tag/v1.0.0). This README covers the firmware on `main`. See the [changelog](CHANGELOG.md) for updates since the release.

## Features

- GL.iNet connected/disconnected client inventory, using the router's explicit online flags.
- Device IP, alias/name, hostname from LAN DNS or the router, MAC address, connection type, and status changes.
- One-minute automatic scans by default and a **Scan now** button.
- Search, sortable columns, clickable summary filters, and responsive phone layouts.
- Light/dark mode and saved 80–150% text size.
- Current time, last completed scan, and live system uptime.
- Configurable timezone, 12/24-hour format, and NTP server; 24-hour time is the default.
- Password-protected Admin with saved Wi-Fi channel and width, restart, and confirmed factory reset.
- Onboard LED activity indicator with an Admin toggle and color selection on supported RGB builds.
- Optional local ARP/ping/TCP discovery and legacy OpenWrt HTTP inventory.

## What you need

| Item | Tested configuration |
| --- | --- |
| Board | LuatOS AirM2M CORE ESP32-C3, 4 MB flash |
| Wi-Fi | 2.4 GHz network |
| IDE | Arduino IDE 2.x |
| Board package | **esp32 by Espressif Systems 3.3.12** |
| Extra library | **LibSSH-ESP32 by Ewan Parker 5.10.0** |
| GL.iNet mode | GL.iNet 4.x router with SSH access and local `clients.get_list` API |

Other ESP32-C3 boards may work but are not release-qualified. No external wiring, display, SD card, Python, Node.js or asset-generation step is needed to upload the included sketch.

## 1. Install Arduino IDE and board support

1. Install [Arduino IDE](https://www.arduino.cc/en/software/).
2. Open **Settings/Preferences → Additional Boards Manager URLs** and add:

   ```text
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```

3. Open **Boards Manager**, search for **esp32**, and install **esp32 by Espressif Systems**, version **3.3.12**.
4. Open **Library Manager**, search for **LibSSH-ESP32**, and install **LibSSH-ESP32 by Ewan Parker**, version **5.10.0**.

WiFi, WebServer, Preferences, ESPmDNS, cJSON and time support come with the Espressif package. Do not install unrelated libraries with similar names. See [Espressif's installation guide](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html) and [LibSSH-ESP32](https://github.com/ewpa/LibSSH-ESP32).

## 2. Download and open the sketch

Use GitHub **Code → Download ZIP**, extract it, then open:

```text
NetworkMonitor/NetworkMonitor.ino
```

Keep **all files inside NetworkMonitor/** together. The folder must remain named `NetworkMonitor`; opening only a copied `.ino` file will fail.

## 3. Select the correct board settings

| Arduino Tools setting | LuatOS CORE ESP32-C3 |
| --- | --- |
| Board | **AirM2M_CORE_ESP32C3** |
| Partition Scheme | **Huge APP (3MB No OTA/1MB SPIFFS)** — required |
| Flash size | **4 MB** |
| Flash mode | **DIO** |
| USB CDC On Boot | **Disabled** for the CH343 USB-to-serial board |
| Port | Your board's USB serial port (`wchusbserial…` on the tested Mac) |
| Erase All Flash Before Sketch Upload | **Disabled** to preserve saved settings |
| Upload speed | Use the default; lower it if uploads are unreliable |

For a native-USB board, USB CDC may need to be **Enabled** instead. If using **ESP32C3 Dev Module** as a fallback, explicitly select 4 MB, DIO and Huge APP; that board profile is not the release-qualified profile.

A charge-only USB cable cannot upload firmware. If no port appears, check the cable and install the USB-serial driver appropriate to the chip fitted to your board, using its manufacturer's driver.

## 4. Upload and provision Wi-Fi

1. Click **Verify**, then **Upload**. If connecting fails, hold **BOOT**, press/release **RST**, begin uploading, then release BOOT once it connects.
2. Open Serial Monitor at **115200 baud** and press **RST** without holding BOOT.
3. On a fresh board, connect your computer/phone to:

   | Setting | Default |
   | --- | --- |
   | Setup SSID | `NetworkMonitor-Setup` |
   | Setup Wi-Fi password | `netmon123` |
   | Configuration URL | `http://192.168.4.1/admin` |
   | Admin username | `admin` |
   | Admin password | `netmonadmin123` |

4. Enter your **2.4 GHz Wi-Fi SSID/password** in Admin, choose a scan source, and save. The system restarts after saving. Wi-Fi channel defaults to **Automatic**. If you select a channel, the ESP32 checks it first. **Channel width** offers 20 MHz or 40 MHz and defaults to 40 MHz. The 40 MHz setting uses 20 MHz when required by the access point. Admin shows the connected channel, width and signal strength.
5. Once connected to Wi-Fi, the setup hotspot closes. Open **http://networkmonitor.local/** or the IP printed in Serial Monitor. Admin is at **/admin**.

The bare hostname **http://networkmonitor/** works only if your router resolves DHCP hostnames. `.local` uses mDNS and may be blocked across guest networks/VLANs. The printed IP is always the direct fallback.

Saved credentials survive normal uploads. The defaults above apply only to fresh setup/factory reset. Send `?` in Serial Monitor to print the current recovery credentials and address. Change the admin password in Admin after setup.

## 5. Configure GL.iNet client monitoring

1. Select **Discovery source → GL.iNet live client list**.
2. Enter the router's LAN IPv4 address, **SSH / port 22** (or its custom port), user **root**, and the router's existing root password.
3. Set the scan interval to **60 seconds**, save, and open the dashboard after restart.

The router must allow SSH password authentication from the ESP32's network. The ESP32 password and router password are separate. Blank password fields preserve their saved values.

This mode reads GL.iNet's **`clients.get_list`** API on router loopback through an authenticated SSH connection reused by scheduled and manual scans. It uses the API's explicit online flag, aliases and disconnected-client history. It **does not run the former SSH `ip neigh` scanner**. Clients are identified by MAC, including when DHCP addresses change. The complete GL.iNet client list is imported; subnet/range filters apply only to local/legacy sources.

Router requirements: its local GL.iNet API, `curl`, `lua` or `luajit`, and a JSON module (`cjson.safe`, `cjson` or `luci.jsonc`). These were present on the tested router; no router package changes were required. Router API errors retain the previous successful report. No fallback invents a replacement status.

The dashboard’s **Hostname / DNS** column is searchable and sortable. It shows the LAN DNS reverse lookup for online devices when available, falling back to an explicit hostname supplied by GL.iNet. Offline entries can show their last known hostname.

See [Router API, SSH identity, limits and legacy OpenWrt setup](docs/ROUTER.md).

## Dashboard and configuration

- Click **Online**, **Offline**, **New**, **Status changes**, or **Tracked devices** to filter the table. Search matches all displayed columns. Click column headings to sort.
- **Scan now** starts a fresh scan; scans do not overlap. Manual requests have a ten-second cooldown. The page fetches results every five seconds.
- The LuatOS onboard **D4 LED** gives a short flash every two seconds while the system is running and flashes rapidly during a scan. Short scans flash for at least three quarters of a second. Use **Admin → Activity light** to turn it on or off without restarting. Boards that expose an onboard `RGB_BUILTIN` LED also show a color picker.
- **System uptime** ticks every second after Last completed scan. It measures this ESP32's time since boot, works without NTP and resets after restart.
- **Date & time** in Admin selects an IANA timezone, 12/24-hour format, and NTP server. Defaults: `America/New_York`, **24 hours**, `time.nist.gov`. Options include `pool.ntp.org`, Cloudflare, Google and a custom hostname/IPv4. NTP needs DNS and outbound UDP 123. Daylight-saving conversion follows the selected timezone.
- **Local ARP / ping / TCP** scans a subnet/range of up to 254 IPv4 addresses. It is an active discovery alternative with different status semantics. Legacy OpenWrt HTTP requires additional router API permissions described in the router guide.
- Settings are stored in ESP32 NVS. Saving restarts the board and clears in-memory discovery history; GL.iNet's router-side disconnected-client list remains on the router.

## Factory reset and recovery

**Admin → Restart system** reboots the ESP32 using the saved settings. Uptime and the in-memory device history start over when the system reconnects.

In Admin, open **Factory reset**, type **RESET**, and submit. This erases the monitor's Wi-Fi/router credentials, admin password, scan/time settings, SSH identity and history. After restart, reconnect to `NetworkMonitor-Setup` using `netmon123`, then sign in as `admin / netmonadmin123`.

If saved Wi-Fi cannot connect for about 30 seconds, the recovery hotspot opens. Use the **current** setup password printed over serial. A normal reset/reupload does not erase NVS.

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| Sketch too large | Select **Huge APP**; the default partition is too small. |
| Garbled serial output | Set Serial Monitor to **115200**, not the upload speed. |
| No hotspot after upload | It closes after successful Wi-Fi connection. Check the LAN IP. If boot fails, check DIO and USB CDC settings. |
| Upload cannot connect | Data cable, correct port, BOOT/RST sequence, and close Serial Monitor. |
| `undefined reference to setup/loop` | Open the complete `NetworkMonitor` sketch folder, not an empty/new sketch or header file. |
| `bad CPU type in executable` on Apple Silicon | Update Arduino IDE and its bundled tools. An older Intel-only `ctags` may require Apple's Rosetta 2; use Apple's installation guidance. Do not replace tools with untrusted binaries. |
| `networkmonitor.local` fails | Use the printed IP; check mDNS and network isolation. |
| SSH identity changed | Verify the router/key/address change before choosing **Forget saved SSH identity** in Admin. |
| GL.iNet API/SSH scan fails | Verify source, router IP/port, root login, API/runtime requirements and reachability. Previous data is retained. |
| Device name is Unknown | The router API did not supply a name. You can set an alias in GL.iNet’s client page. |
| Hostname / DNS is Unavailable | The device has no known hostname from LAN DNS or the router. An offline device can retain its last known hostname. |
| Status differs from physical power state | The display follows GL.iNet's own tracker freshness/history. It is not a separate physical connectivity test. |
| Clock pending | Check DNS and UDP 123; try another configured NTP server. Uptime still works. |

## Security and operating limits

Use on a trusted LAN. The ESP32 dashboard is public to reachable LAN clients; Admin uses HTTP Digest and form tokens, but HTTP does not encrypt settings submissions. Do not expose the ESP32 to the internet. Passwords are stored in unencrypted Preferences and printed to the USB console for recovery. Default passwords are documented publicly.

The router connection uses SSH and a saved host-key fingerprint (trust on first successful login). The loopback API request accepts the router's self-signed HTTPS certificate inside that authenticated SSH connection. The request is read-only; router configuration/client records are not modified.

Limits: **254 stored clients**, 1535 bytes per reduced API record, 2 MB transferred report, GL.iNet API timeout 20 seconds, SSH read budget 50 seconds. There is no OTA update feature. First-scan devices count as new. Empty or removed GL.iNet entries follow the authoritative router list. Read failures retain the last report.

## Development and validation

See [Development and release checks](docs/DEVELOPMENT.md). End users can upload directly; generated compressed assets are already included.

The current LuatOS build was compiled with Arduino-ESP32 3.3.12 and LibSSH-ESP32 5.10.0. It was flashed to a LuatOS ESP32-C3 and published a live GL.iNet scan with 115 clients. The RGB build was compile-tested. See the [router guide](docs/ROUTER.md#validation) for hardware verification details.

## License

Project source: [MIT](LICENSE). Dependencies retain their own licenses; see [third-party notices](THIRD_PARTY.md).
