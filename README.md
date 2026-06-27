# ESP32-S3 USB-HID Keyboard Dongle

Turn an **ESP32-S3** board into a remote keyboard: plug it into a target computer
where it enumerates as a real **USB HID keyboard**, then send text to it from another
machine over **BLE** or **WiFi**. The text is injected as live keystrokes.

```
[Control machine] --BLE / WiFi--> [ESP32-S3 dongle] --USB HID keyboard--> [Target PC]
```

> Needs an **ESP32-S3** (or S2). The USB-keyboard trick requires native **USB-OTG**,
> which the S-series has and the C-series (C3/C5/C6) does **not**.

## Supported boards

One firmware (`firmware/keyboard-dongle`) targets all three — pick yours with a single
`#define BOARD` (or `-DBOARD=n`). The only difference between them is the screen.

| Board | `BOARD` | Screen | Notes |
|-------|---------|--------|-------|
| **LilyGo T-Dongle S3**     | `1` | ST7735 80×160  | USB-A plug + screen |
| **Waveshare ESP32-S3-Geek**| `2` | ST7789 240×135 | USB-A plug + screen (default) |
| **M5Stack AtomS3U**        | `3` | none           | USB-A plug; fixed WiFi creds, no on-screen display |

### Verification status

All three share one firmware, but only the Geek has been run on real hardware so far:

| Board | Status | Verified on hardware |
|-------|--------|----------------------|
| **Waveshare ESP32-S3-Geek** | ✅ **Hardware-tested** | USB keyboard, USB mouse, screen, button WiFi↔BLE toggle, and WiFi + BLE control all confirmed working. |
| **LilyGo T-Dongle S3** | ⚠️ **Compile-only** | Builds clean; same firmware. ST7735 pins/init are the documented ones but **not yet verified on the panel**. |
| **M5Stack AtomS3U** | ⚠️ **Compile-only** | Builds clean; same firmware. No screen (fixed pass `type12345`); button pin (GPIO41) **unverified**. |

On the untested boards, USB-HID + WiFi + BLE should work (they're chip-level, proven on
the Geek); the screen/button wiring is what may need a small per-board tweak.

## Quick start (one-click flasher, no toolchain)

1. In **Chrome/Edge**, open **https://kolec94.github.io/tdongle-s3-hid/web/**
2. Pick your board, hold the board's **BOOT** button, plug it in, click **Install**.
3. Plug it into the **target** computer. With a screen, it shows the WiFi
   name/password; screenless boards use a fixed password (below).
4. Connect from your phone/laptop over WiFi or BLE to **type and move the mouse**.
   Press the board's button to switch **WiFi ⇄ BLE** (screen shows the active mode).

**Screenless (AtomS3U) default credentials:** WiFi SSID `Keeb-XXXX` (XXXX = chip id,
visible in the WiFi list), password **`type12345`**, page at `http://192.168.4.1`.

## Build from source

Arduino IDE or arduino-cli, **esp32 core 3.x**. Board **ESP32S3 Dev Module** with:
USB CDC On Boot **Enabled**, USB Mode **USB-OTG (TinyUSB)**, Partition Scheme
*16M Flash (3MB APP/9.9MB FATFS)*, Flash Size **16MB**. Libraries: **Adafruit GFX**,
**Adafruit ST7735 and ST7789**.

Set your board at the top of `firmware/keyboard-dongle/keyboard-dongle.ino`
(`#define BOARD ...`), then upload. Or headless per board:

```bash
arduino-cli compile \
  --fqbn "esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=cdc,PartitionScheme=app3M_fat9M_16MB,FlashSize=16M" \
  --build-property "compiler.cpp.extra_flags=-DBOARD=2" \
  firmware/keyboard-dongle
# BOARD=1 LilyGo, 2 Geek, 3 AtomS3U
```

> **AtomS3U has only 8 MB flash** (vs 16 MB on the others), so build it with
> `FlashSize=8M` + `PartitionScheme=default_8MB`. `collect-bins.ps1` does this
> automatically per board.

**CI:** a GitHub Action (`.github/workflows/build-firmware.yml`) rebuilds all three
boards and commits the refreshed `web/<board>/` bins on any push that touches
`firmware/`. So a normal `git push` keeps the hosted flasher in sync — `collect-bins.ps1`
stays for local/offline builds.

## Files

| Path | What |
|------|------|
| `firmware/keyboard-dongle/` | The one firmware, board-selectable via `BOARD`. |
| `web/index.html` | Hosted page: flash any board + drive it over BLE. |
| `web/control.html` | Standalone BLE keyboard control page. |
| `web/<board>/` + `web/manifest-*.json` | Pre-built bins per board for the flasher. |
| `collect-bins.ps1` | Local build of all three boards' bins into `web/`. |
| `.github/workflows/build-firmware.yml` | CI: rebuild + commit bins on push. |
| `CHANGELOG.md` | History of the project and changes. |

## Notes / caveats

- **Controls:** the device is a composite USB **keyboard + mouse**. Drive it from the
  hosted **`control.html`** over BLE — text, special keys, a **mouse trackpad** (drag to
  move, tap to click), click/scroll buttons, and a **sensitivity slider**. The on-device
  page at `http://192.168.4.1` (WiFi mode) currently does **keyboard only** (no mouse UI
  yet, though the firmware has a `/mouse` endpoint).
- **Button switches the radio:** the BOOT button toggles **WiFi ⇄ BLE**; only one is live
  at a time and the screen shows which. (It does *not* auto-switch on connect.)
- **BLE is a data service, not an OS keyboard.** Connect via `control.html` (Web
  Bluetooth) — it will **not** show in your OS Bluetooth "add device" list, by design.
  Web Bluetooth works in **Chrome/Edge** (Windows/macOS/Linux/Android); not iOS or Safari/Firefox.
- **BLE pairing/security is OFF by default** (`REQUIRE_BLE_PAIRING 0`) for reliable first
  boot. Set `1` (needs a screen) for an on-screen pairing PIN — less tested.
- **US keyboard layout** is assumed on the target for symbols.
- **Screens:** the Geek (ST7789) is hardware-verified. If another board's screen is
  blank/garbled, tweak the `#if USE_ST77xx` block in `setup()` (rotation/invert/offset);
  ST7789 needs the **software-SPI constructor** (hardware-SPI re-init lands on the wrong
  pins → blank). USB-HID + WiFi + BLE work regardless of the screen.
- **Flashing an S3:** while the firmware is running it won't auto-reset into the
  bootloader over native USB — **hold BOOT while plugging in** to force download mode,
  then flash. (The running app also re-enumerates to a different COM port than the bootloader.)
