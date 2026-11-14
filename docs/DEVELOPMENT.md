# Development

## Layout

- `NetworkMonitor/`: complete Arduino sketch, router transports/parsers, time configuration and generated web assets.
- `web/`: editable dashboard/Admin HTML, JavaScript, styles and timezone menu data.
- `tools/build_web_assets.py`: generates gzip arrays in `NetworkMonitor/WebAssets.h`.
- `tests/`: parser, transport, time, browser behavior and Lua reducer fixtures.
- `docs/ROUTER.md`: GL.iNet API and optional legacy OpenWrt setup.
- `docs/openwrt-http-acl.json`: optional ACL for the legacy HTTP source only; not required by GL.iNet mode.

Do not delete the generated headers: Arduino IDE users need them without a separate web build.

## Build

Install [Arduino CLI](https://arduino.github.io/arduino-cli/) and run from the repository root:

```sh
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@3.3.12 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli lib install LibSSH-ESP32@5.10.0
python3 tools/build_web_assets.py
arduino-cli compile --fqbn esp32:esp32:AirM2M_CORE_ESP32C3:PartitionScheme=huge_app NetworkMonitor
```

Python 3 is required only when modifying web assets. Regenerate assets after edits; generated gzip content has a deterministic timestamp. Keep `web/timezones.json` and `NetworkMonitor/TimeZones.h` aligned if changing timezone choices. `.clang-format` records C++ formatting preferences. Firmware behavior changes should have focused fixtures and a changelog entry.

To compile the optional RGB controls, use a board variant that exposes `RGB_BUILTIN`. The generic ESP32-C3 target is used for this compile check.

```sh
arduino-cli compile --fqbn esp32:esp32:esp32c3:PartitionScheme=huge_app NetworkMonitor
```

## Tests

Browser behavior tests require Node.js (development only):

```sh
node tests/web_test.js
node tests/admin_test.js
```

C++ tests require a C++17 compiler and cJSON development headers/library. For example, on a system with `pkg-config` and libcjson installed:

```sh
c++ -std=c++17 tests/gl_client_stream_test.cpp $(pkg-config --cflags --libs libcjson) -o /tmp/netmon-clients
/tmp/netmon-clients
c++ -std=c++17 tests/router_ssh_report_test.cpp $(pkg-config --cflags --libs libcjson) -o /tmp/netmon-ssh
/tmp/netmon-ssh
c++ -std=c++17 tests/openwrt_data_test.cpp $(pkg-config --cflags --libs libcjson) -o /tmp/netmon-openwrt
/tmp/netmon-openwrt
c++ -std=c++17 tests/time_settings_test.cpp -o /tmp/netmon-time
/tmp/netmon-time
```

The exact on-router Lua reducer can be tested with Python and Lupa, without a router:

```sh
python3 -m venv /tmp/netmon-tests
/tmp/netmon-tests/bin/pip install lupa
/tmp/netmon-tests/bin/python tests/router_command_test.py
```

## Release checklist

1. Run the host checks, regenerate assets, and compile with the pinned toolchain.
2. Compare board settings and dependencies against README.
3. Verify scheduled and manual GL.iNet scans reuse the SSH login, errors retain the latest report, and hostnames display separately from aliases. Check Admin login, time synchronization and system uptime.
4. Verify the Wi-Fi settings after a restart, then test the activity-light toggle and persistence. Check the color picker and actual LED color on supported RGB hardware.
5. Verify public files contain no personal Wi-Fi/router credentials, host fingerprints, real inventories or build artifacts.
6. Update VERSION and CHANGELOG; create a version tag after committing.
7. Test destructive factory reset only on a disposable configuration or spare board. Do not wipe an in-use board just to validate a release.

GitHub releases contain source archives. Build binaries locally using the documented board settings. Never publish an NVS/whole-flash backup: it can contain passwords and router trust data.
