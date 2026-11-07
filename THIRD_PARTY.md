# Third-party components

Dependencies are installed separately, not vendored into this repository:

- [Arduino-ESP32](https://github.com/espressif/arduino-esp32): Espressif core and bundled SDK/components; see its upstream license files.
- [LibSSH-ESP32](https://github.com/ewpa/LibSSH-ESP32): port of libssh; see upstream `COPYING` and `BSD` files. The project's MIT license does not replace dependency license obligations.
- cJSON: bundled with the Espressif environment; see [upstream MIT license](https://github.com/DaveGamble/cJSON).
- IANA timezone identifiers: used as configuration names; actual timezone conversion uses the viewing browser's Intl implementation.

GL.iNet firmware and APIs belong to their respective owners. This project is independent and is not endorsed by GL.iNet or Espressif.
