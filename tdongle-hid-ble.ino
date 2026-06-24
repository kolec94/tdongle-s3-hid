/*
 * T-Dongle S3 — BLE-to-USB-HID keyboard
 *
 * Plug the dongle into the TARGET computer. It enumerates as a USB keyboard
 * and advertises a BLE "Nordic UART" service. A control computer connects
 * over BLE (e.g. via the Web Bluetooth page, control.html) and sends text;
 * the dongle types it into the target over USB HID.
 *
 * No WiFi is used, so the control computer stays on its normal network.
 * Note: ESP32-S3 is BLE-only (no Bluetooth Classic / SPP).
 *
 * Board:  ESP32S3 Dev Module
 * Tools:  USB CDC On Boot -> Enabled
 *         USB Mode        -> USB-OTG (TinyUSB)
 *         Upload Mode     -> UART0 / Hardware CDC
 *         PSRAM           -> OPI PSRAM
 *         Partition Scheme-> a scheme with BLE space, e.g. "16M Flash (3MB APP/9.9MB FATFS)"
 *
 * Requires esp32 Arduino core 3.x (uses the String-based BLE API).
 */

#include "USB.h"
#include "USBHIDKeyboard.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

USBHIDKeyboard Keyboard;

// Nordic UART Service (NUS) UUIDs
#define SERVICE_UUID  "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_RX_UUID  "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // control -> dongle (write)
#define CHAR_TX_UUID  "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // dongle -> control (notify, optional)

const char *DEVICE_NAME = "TDongle-HID";

BLECharacteristic *txChar = nullptr;
bool deviceConnected = false;

// Send a single special key / combo (used for messages prefixed with 0x01)
void handleKey(const String &k) {
  if (k == "ENTER")      Keyboard.write(KEY_RETURN);
  else if (k == "TAB")   Keyboard.write(KEY_TAB);
  else if (k == "ESC")   Keyboard.write(KEY_ESC);
  else if (k == "WIN")   Keyboard.write(KEY_LEFT_GUI);
  else if (k == "CTRL_ALT_DEL") {
    Keyboard.press(KEY_LEFT_CTRL);
    Keyboard.press(KEY_LEFT_ALT);
    Keyboard.press(KEY_DELETE);
    delay(60);
    Keyboard.releaseAll();
  }
}

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    String v = c->getValue();              // esp32 core 3.x returns String
    if (v.length() == 0) return;
    if (v[0] == '\x01') {                   // 0x01 prefix = special-key command
      handleKey(v.substring(1));
    } else {
      Keyboard.print(v);                    // print() handles '\n' as Enter
    }
  }
};

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *s) override { deviceConnected = true; }
  void onDisconnect(BLEServer *s) override {
    deviceConnected = false;
    BLEDevice::startAdvertising();          // allow reconnects
  }
};

void setup() {
  // USB HID keyboard
  Keyboard.begin();
  USB.begin();

  // BLE
  BLEDevice::init(DEVICE_NAME);
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService *svc = server->createService(SERVICE_UUID);

  BLECharacteristic *rxChar = svc->createCharacteristic(
      CHAR_RX_UUID,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rxChar->setCallbacks(new RxCallbacks());

  txChar = svc->createCharacteristic(
      CHAR_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  txChar->addDescriptor(new BLE2902());

  svc->start();

  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
}

void loop() {
  delay(50);
}
