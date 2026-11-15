# Changelog

## Unreleased

- Fix scan-table allocation failures caused by fragmented heap by growing and reusing small blocks.
- Support up to 255 devices and allocate names, hostnames, and connection labels according to their actual length.
- Use the onboard LED for a heartbeat and scan activity, with an Admin on/off setting and a color picker on supported RGB boards.
- Reuse authenticated GL.iNet SSH and legacy OpenWrt RPC sessions across scans. Reconnect when the session drops or expires.
- Show searchable and sortable LAN DNS hostnames separately from device names and aliases.
- Add station channel and 20/40 MHz channel width settings in Admin.
- Add an authenticated restart action that keeps saved settings.
- Stream dashboard inventory responses and reduce scanner memory use to leave more room for SSH on the ESP32-C3.
- Label uptime and restart text with “system”.

## 1.0.0 — 2026-11-06

First production release, accepted on the LuatOS CORE ESP32-C3.

- GL.iNet client API inventory with explicit connected/disconnected status and MAC-based identity.
- Names/aliases, connection types, status transitions, search, sortable columns and summary filters.
- Responsive dashboard/Admin, themes and adjustable text size.
- Configurable time settings and live ESP32 uptime.
- Saved configuration, recovery setup AP and confirmed factory reset.
- Local probing and legacy OpenWrt HTTP alternatives.
- Removed the obsolete SSH neighbor scanner; retained the validated runtime build identifier `glinet-ui-5`.
