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
| `tdongle-hid-dual/`   | **Recommended firmware.** BLE *and* WiFi AP at once, on-screen WiFi password + BLE PIN, LCD status, exclusive-mode (whichever connects disables the other). This is what the hosted `.bin` files are built from. |
| `tdongle-hid-ble/`    | Minimal firmware: BLE (Nordic UART) → USB HID only. |
| `tdongle-hid-wifi/`   | Minimal firmware: WiFi access point + web page → USB HID only. |
| `web/index.html`      | Root page: flash the dongle (ESP Web Tools) **and** drive it over BLE. |
| `web/control.html`    | Standalone keyboard-only control page (same UI, linked from the root page). |
| `web/manifest.json`   | ESP Web Tools manifest (points at the 4 `.bin` files in `web/`). |
| `web/*.bin`           | Pre-built firmware images used by the browser flasher (optional; rebuildable from source — see below). |
| `collect-bins.ps1`    | Gathers freshly built firmware binaries into `web/` for hosting. |

## Quick start (no toolchain needed)

1. Open the hosted page in **desktop Chrome or Edge**:
   **https://kolec94.github.io/tdongle-s3-hid/web/**
2. Hold the dongle's **BOOT** button, plug it into this computer, click **Install firmware**.
3. Move the dongle to the **target** computer. Back on the page (or
   [the standalone control page](https://kolec94.github.io/tdongle-s3-hid/web/control.html)),
   click **Connect to dongle**, type, and **Send**.

## Build & flash from source (Arduino IDE)

1. Arduino IDE with the **esp32 core 3.x** installed. Board **ESP32S3 Dev Module** with:
   - USB CDC On Boot: **Enabled**
   - USB Mode: **USB-OTG (TinyUSB)**
   - Partition Scheme: one with BLE room, e.g. *16M Flash (3MB APP / 9.9MB FATFS)*
   - Flash Size: **16MB**
2. Open `tdongle-hid-dual/tdongle-hid-dual.ino` (the full BLE+WiFi build; or one of
   the minimal `tdongle-hid-ble` / `tdongle-hid-wifi` sketches), hold the dongle's
   **BOOT** button while plugging in, then **Upload**.

   The dual build needs the **Adafruit GFX** and **Adafruit ST7735 and ST7789**
   libraries for the LCD (Library Manager, or `arduino-cli lib install`).

That flashes the dongle directly — you don't need the `.bin` files for this path.

## Rebuilding the flashable `.bin` files (optional)

The `web/` folder ships pre-built binaries so the browser **Install firmware**
button works without any toolchain. If you'd rather build them yourself (to audit
the firmware, or after changing the source), regenerate them one of two ways. Both
produce four files in `web/`: `bootloader.bin`, `partitions.bin`, `boot_app0.bin`,
`firmware.bin`.

### Option A — Arduino IDE

1. Open `tdongle-hid-dual/tdongle-hid-dual.ino`, set the board options as in *Build & flash* above.
2. **Sketch → Export Compiled Binary** (writes a `build/` folder next to the sketch).
3. Run the collector:
   ```powershell
   .\collect-bins.ps1
   ```

### Option B — arduino-cli (headless)

```bash
# one-time setup
arduino-cli config init
arduino-cli config add board_manager.additional_urls \
  https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32
arduino-cli lib install "Adafruit GFX Library" "Adafruit ST7735 and ST7789 Library"

# build with the USB-OTG (TinyUSB) + CDC-on-boot + BLE-capable partition options
arduino-cli compile \
  --fqbn "esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=cdc,PartitionScheme=app3M_fat9M_16MB,FlashSize=16M" \
  --output-dir tdongle-hid-dual/build \
  tdongle-hid-dual

# gather the 4 bins into web\ (PowerShell)
.\collect-bins.ps1
```

Then commit the updated `web/*.bin` (and re-host) to refresh the one-click flasher.

## Notes / caveats

- ESP32-S3 is **BLE only** (no Bluetooth Classic / SPP).
- Web Bluetooth + Web Serial work in **desktop Chrome / Edge** (Win, macOS, Linux)
  and Android Chrome. Not Safari/Firefox; not iOS (use a BLE UART app there).
- HID keycodes assume a **US keyboard layout** on the target.
- **Dual firmware security:** the WiFi password and BLE PIN are random per boot and
  shown only on the LCD. Set `REQUIRE_BLE_PAIRING 0` in the sketch to drop the BLE
  PIN (Just Works) if pairing gives you trouble on a particular OS.
- **LCD config:** the dual sketch assumes the T-Dongle S3 ST7735 pinout and uses
  `INITR_MINI160x80` + `invertDisplay(true)`. If the screen is blank, offset, or has
  wrong colors, adjust the init tab / rotation / inversion at the top of `setup()`.
- The minimal `tdongle-hid-ble` / `tdongle-hid-wifi` sketches have **no auth** as
  written — fine for quick use, but add a passkey/password if you need it locked down.
