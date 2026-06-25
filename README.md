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

## Quick start (one-click flasher, no toolchain)

1. In **Chrome/Edge**, open **https://kolec94.github.io/tdongle-s3-hid/web/**
2. Pick your board, hold the board's **BOOT** button, plug it in, click **Install**.
3. Plug it into the **target** computer. With a screen, it shows the WiFi
   name/password; screenless boards use a fixed password (below).
4. Connect from your phone/laptop over WiFi or BLE and type.

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

## Files

| Path | What |
|------|------|
| `firmware/keyboard-dongle/` | The one firmware, board-selectable via `BOARD`. |
| `web/index.html` | Hosted page: flash any board + drive it over BLE. |
| `web/control.html` | Standalone BLE keyboard control page. |
| `web/<board>/` + `web/manifest-*.json` | Pre-built bins per board for the flasher. |
| `collect-bins.ps1` | Builds all three boards' bins into `web/`. |

## Notes / caveats

- **BLE pairing security is OFF by default** (`REQUIRE_BLE_PAIRING 0`) so the device
  boots reliably and BLE control just works. Set it to `1` (needs a screen) to require
  an on-screen pairing PIN — that path is less tested on these exact boards.
- HID keycodes assume a **US keyboard layout** on the target.
- The **screen config** (ST7735/ST7789 rotation, inversion, offsets) is set from each
  board's documented pinout but verify on hardware — if blank/garbled, tweak the
  `#if USE_ST77xx` block in `setup()`. USB-HID + WiFi + BLE work regardless of the screen.
- Whichever transport (BLE or WiFi) connects first disables the other until it
  disconnects, so only one control path is live at a time.
