# Changelog

## Reconstruction — multi-board ESP32-S3 (current)

The project was reorganized around **three ESP32-S3 boards**, sharing one firmware.

- **Added** `firmware/keyboard-dongle/` — a single sketch that targets all three boards
  via `#define BOARD` (or `-DBOARD=n`). USB-HID + WiFi + BLE are identical across
  boards; only the screen driver differs:
  - `1` LilyGo T-Dongle S3 — ST7735 80×160
  - `2` Waveshare ESP32-S3-Geek — ST7789 240×135 (pins verified from Waveshare/Tasmota docs)
  - `3` M5Stack AtomS3U — no screen (fixed WiFi password `type12345`)
- **Added** a board-picker web flasher (`web/index.html` + `manifest-<board>.json` +
  `web/<board>/` bins) hosted at https://kolec94.github.io/tdongle-s3-hid/web/
- **Changed** `REQUIRE_BLE_PAIRING` default to `0` (open BLE control) for reliable first
  boot; set to `1` to require an on-screen pairing PIN.
- **Removed** the ESP32-C5 sketches and the superseded single-mode S3 sketches.
- **Status:** firmware compiles for all three boards; **not yet run on physical S3
  hardware**. USB-HID + WiFi are well-trodden on S3; screen config may need a small
  tweak per board once tested.

## Background — why it changed

- Project began as a USB-HID keyboard for the **LilyGo T-Dongle S3**.
- Hardware ordered turned out to be an **ESP32-C5**, which has **no USB-OTG** and so
  physically cannot be a USB keyboard (only ESP32-S2/S3 can).
- A wireless proof-of-concept was built on the C5 (WiFi web page → BLE HID keyboard).
  It worked on Linux but was unreliable: BLE dropped under WiFi+BLE coexistence and
  couldn't auto-reconnect because BLE bonding crashes the C5's BLE stack in
  arduino-esp32 3.3.10 (and NimBLE-Arduino won't link against the C5 core).
- Decision: keep the C5 as a dual-band WiFi analyzer; get **ESP32-S3** hardware for the
  real wired-USB keyboard. Three S3 boards were ordered, hence this multi-board layout.
