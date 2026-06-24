# T-Dongle S3 — USB-HID Keyboard over BLE / WiFi

Turn a [LILYGO T-Dongle S3](https://www.lilygo.cc/products/t-dongle-s3) (ESP32-S3)
into a remote keyboard: plug it into a target computer where it enumerates as a
USB HID keyboard, then send text to it from another machine over **BLE** (no
network needed) or **WiFi**. The text is injected as real keystrokes into the
target.

```
[Control machine] --BLE / WiFi--> [T-Dongle S3] --USB HID keyboard--> [Target PC]
```

## Contents

| File | What it is |
|------|------------|
| `tdongle-hid-ble.ino` | Firmware: BLE (Nordic UART) → USB HID. Control machine stays on its own network. |
| `tdongle-hid.ino`     | Firmware: WiFi access point + web page → USB HID. |
| `control.html`        | Standalone Web Bluetooth control page for the BLE firmware. |
| `web/index.html`      | Combined page: flash the dongle (ESP Web Tools) **and** drive it over BLE. |
| `web/manifest.json`   | ESP Web Tools manifest (expects the 4 `.bin` files alongside it). |
| `collect-bins.ps1`    | Gathers the exported firmware binaries into `web/` for hosting. |

## Build & flash

1. Arduino IDE, esp32 core 3.x. Board **ESP32S3 Dev Module** with:
   - USB CDC On Boot: **Enabled**
   - USB Mode: **USB-OTG (TinyUSB)**
   - Partition Scheme: one with BLE room, e.g. *16M Flash (3MB APP / 9.9MB FATFS)*
2. Open `tdongle-hid-ble.ino`, hold the dongle's BOOT button while plugging in, Upload.

To enable browser flashing via `web/index.html`: **Sketch → Export Compiled
Binary**, run `collect-bins.ps1`, then host the `web/` folder over HTTPS.

## Notes / caveats

- ESP32-S3 is **BLE only** (no Bluetooth Classic / SPP).
- Web Bluetooth + Web Serial work in **desktop Chrome / Edge** (Win, macOS, Linux)
  and Android Chrome. Not Safari/Firefox; not iOS (use a BLE UART app there).
- HID keycodes assume a **US keyboard layout** on the target.
- No auth on the BLE link as written — anyone in range can connect. Add a
  passkey/bonding if you need it locked down.
