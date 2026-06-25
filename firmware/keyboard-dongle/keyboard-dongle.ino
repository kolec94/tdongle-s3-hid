/*
 * ESP32-S3 USB-HID keyboard dongle — ONE firmware, THREE boards.
 *
 * Plug into the TARGET computer; it enumerates as a real USB keyboard. At boot it
 * brings up a BLE (Nordic UART) service AND a WiFi access point. Send text from a
 * control machine over whichever is convenient:
 *   - WiFi : join the AP, open http://192.168.4.1
 *   - BLE  : use web/control.html (Web Bluetooth)
 * Whichever transport connects first disables the other until it disconnects.
 *
 * Pick your board below (or pass -DBOARD=n to arduino-cli). The ONLY thing that
 * differs between boards is the on-board screen; USB-HID + WiFi + BLE are identical.
 *
 * Board options (all are ESP32-S3, which is what makes USB-HID possible):
 *   USB CDC On Boot : Enabled
 *   USB Mode        : USB-OTG (TinyUSB)
 *   Partition Scheme: 16M Flash (3MB APP/9.9MB FATFS)
 *   Flash Size      : 16MB
 * Libraries: Adafruit GFX, Adafruit ST7735 and ST7789.
 */

#include "USB.h"
#include "USBHIDKeyboard.h"
#include <WiFi.h>
#include <WebServer.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLESecurity.h>
#include <SPI.h>
#include <esp_mac.h>
#include <esp_random.h>

// ===================== BOARD SELECT =====================
#define BOARD_LILYGO_TDONGLE_S3 1   // ST7735 80x160 LCD, USB-A plug
#define BOARD_WAVESHARE_S3_GEEK 2   // ST7789 240x135 LCD, USB-A plug
#define BOARD_M5STACK_ATOMS3U   3   // no screen (RGB LED), USB-A plug
#ifndef BOARD
#define BOARD BOARD_WAVESHARE_S3_GEEK   // <-- change to your board, or pass -DBOARD=n
#endif

// 0 = open BLE control (reliable first boot, recommended until tested on hardware)
// 1 = require an on-screen pairing PIN (needs a display + bonding)
#define REQUIRE_BLE_PAIRING 0

// ----- per-board wiring -----
#if BOARD == BOARD_LILYGO_TDONGLE_S3
  #define HAS_DISPLAY 1
  #define USE_ST7735  1
  #define TFT_CS 4
  #define TFT_DC 2
  #define TFT_RST 1
  #define TFT_MOSI 3
  #define TFT_SCLK 5
  #define TFT_BL 38
  #define BOARD_NAME "LilyGo T-Dongle S3"
#elif BOARD == BOARD_WAVESHARE_S3_GEEK
  #define HAS_DISPLAY 1
  #define USE_ST7789  1
  #define TFT_CS 10
  #define TFT_DC 12
  #define TFT_RST 7
  #define TFT_MOSI 11
  #define TFT_SCLK 8
  #define TFT_BL 9
  #define BOARD_NAME "Waveshare ESP32-S3-Geek"
#elif BOARD == BOARD_M5STACK_ATOMS3U
  #define HAS_DISPLAY 0
  #define BOARD_NAME "M5Stack AtomS3U"
#else
  #error "Unknown BOARD"
#endif

#if REQUIRE_BLE_PAIRING && !HAS_DISPLAY
  #error "REQUIRE_BLE_PAIRING needs a display to show the PIN; this board has none."
#endif

#include <Adafruit_GFX.h>
#if USE_ST7735
  #include <Adafruit_ST7735.h>
#elif USE_ST7789
  #include <Adafruit_ST7789.h>
#endif

// ---- Nordic UART Service UUIDs (match web/control.html) ----
#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_RX_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_TX_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

USBHIDKeyboard Keyboard;
WebServer server(80);
#if USE_ST7735
Adafruit_ST7735 tft = Adafruit_ST7735(&SPI, TFT_CS, TFT_DC, TFT_RST);
#elif USE_ST7789
Adafruit_ST7789 tft = Adafruit_ST7789(&SPI, TFT_CS, TFT_DC, TFT_RST);
#endif
BLEAdvertising *advertising = nullptr;

String apSsid, apPass;
volatile uint32_t g_pairPin = 0;
volatile bool g_showPin = false;
volatile bool needRedraw = false;
enum Mode { MODE_IDLE, MODE_WIFI, MODE_BLE };
Mode current = MODE_IDLE;
volatile bool bleConnected = false;
volatile int wifiClients = 0;
bool wifiApUp = false;

const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>S3 Keyboard</title><style>
body{font-family:system-ui,sans-serif;max-width:640px;margin:24px auto;padding:0 16px;background:#111;color:#eee}
h1{font-size:1.2rem}
textarea{width:100%;height:150px;font-size:16px;padding:10px;box-sizing:border-box;background:#1c1c1c;color:#eee;border:1px solid #444;border-radius:8px}
.row{display:flex;gap:8px;flex-wrap:wrap;margin-top:10px;align-items:center}
button{font-size:15px;padding:10px 16px;border:0;border-radius:8px;cursor:pointer;background:#3b82f6;color:#fff}
button.alt{background:#374151}label{display:flex;align-items:center;gap:6px;font-size:14px}
#log{margin-top:12px;font-size:13px;color:#9ca3af}
</style></head><body>
<h1>ESP32-S3 — Remote Keyboard (WiFi)</h1>
<textarea id="txt" placeholder="Type text to send to the target computer..."></textarea>
<div class="row"><button onclick="send()">Send</button>
<label><input type="checkbox" id="enter" checked> Press Enter after</label></div>
<div class="row">
<button class="alt" onclick="key('ENTER')">Enter</button>
<button class="alt" onclick="key('TAB')">Tab</button>
<button class="alt" onclick="key('ESC')">Esc</button>
<button class="alt" onclick="key('WIN')">Win/Cmd</button>
<button class="alt" onclick="key('CTRL_ALT_DEL')">Ctrl+Alt+Del</button></div>
<div id="log"></div><script>
async function post(p,b){const r=await fetch(p,{method:'POST',headers:{'Content-Type':'text/plain'},body:b});
document.getElementById('log').textContent=r.ok?'sent':'error '+r.status;}
function send(){let t=document.getElementById('txt').value;if(document.getElementById('enter').checked)t+='\n';post('/type',t);}
function key(k){post('/key',k);}
</script></body></html>
)HTML";

void doKey(const String &k) {
  if (k == "ENTER")      Keyboard.write(KEY_RETURN);
  else if (k == "TAB")   Keyboard.write(KEY_TAB);
  else if (k == "ESC")   Keyboard.write(KEY_ESC);
  else if (k == "WIN")   Keyboard.write(KEY_LEFT_GUI);
  else if (k == "CTRL_ALT_DEL") {
    Keyboard.press(KEY_LEFT_CTRL); Keyboard.press(KEY_LEFT_ALT); Keyboard.press(KEY_DELETE);
    delay(60); Keyboard.releaseAll();
  }
}

#if HAS_DISPLAY
void screen(const String &l1, uint16_t c1, const String &l2, const String &l3, const String &l4) {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(c1);           tft.setCursor(2, 2);  tft.println(l1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(2, 20); tft.println(l2);
  tft.setCursor(2, 34); tft.println(l3);
  tft.setCursor(2, 48); tft.println(l4);
}
void drawScreen() {
  if (g_showPin) {
    char pin[8]; snprintf(pin, sizeof(pin), "%06u", (unsigned)g_pairPin);
    screen("BLE PAIRING", ST77XX_MAGENTA, "Enter this PIN on", "your control device:", String(pin));
    return;
  }
  if (current == MODE_BLE) {
    screen("BLE CONNECTED", ST77XX_GREEN, "Typing ready.", "WiFi disabled", "");
  } else if (current == MODE_WIFI) {
    screen("WIFI CONNECTED", ST77XX_CYAN, "Open in browser:",
           "http://" + WiFi.softAPIP().toString(), "");
  } else {
    screen(BOARD_NAME, ST77XX_YELLOW, "WiFi: " + apSsid, "Pass: " + apPass,
           REQUIRE_BLE_PAIRING ? "BLE: PIN at pairing" : "BLE: open");
  }
}
#else
void drawScreen() {}   // screenless board: nothing to draw
#endif

void genCreds() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  char ss[20];
  snprintf(ss, sizeof(ss), "Keeb-%02X%02X", mac[4], mac[5]);
  apSsid = ss;
#if HAS_DISPLAY
  static const char cs[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz23456789";
  apPass = "";
  for (int i = 0; i < 8; i++) apPass += cs[esp_random() % (sizeof(cs) - 1)];  // random, shown on screen
#else
  apPass = "type12345";   // fixed: no screen to display a random one
#endif
  Serial.printf("[%s] WiFi: %s / %s  ->  http://192.168.4.1\n", BOARD_NAME, apSsid.c_str(), apPass.c_str());
}

void startAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid.c_str(), apPass.c_str());
  wifiApUp = true;
}
void stopAP() {
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiApUp = false;
  wifiClients = 0;
}
void onWiFiEvent(WiFiEvent_t e) {
  if (e == ARDUINO_EVENT_WIFI_AP_STACONNECTED) wifiClients++;
  else if (e == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED && wifiClients > 0) wifiClients--;
}

class SrvCb : public BLEServerCallbacks {
  void onConnect(BLEServer *s) override { bleConnected = true; }
  void onDisconnect(BLEServer *s) override { bleConnected = false; }
};
class RxCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    if (g_showPin) { g_showPin = false; needRedraw = true; }
    String v = c->getValue();
    if (v.length() == 0) return;
    if (v[0] == '\x01') doKey(v.substring(1));
    else                Keyboard.print(v);
  }
};
#if REQUIRE_BLE_PAIRING
class SecCb : public BLESecurityCallbacks {
  uint32_t onPassKeyRequest() override { return 0; }
  void onPassKeyNotify(uint32_t pk) override { g_pairPin = pk; g_showPin = true; needRedraw = true; }
  bool onConfirmPIN(uint32_t pk) override { return true; }
  bool onSecurityRequest() override { return true; }
};
#endif

void updateMode() {
  Mode desired = bleConnected ? MODE_BLE : (wifiClients > 0 ? MODE_WIFI : MODE_IDLE);
  if (desired == current) return;
  current = desired;
  if (desired == MODE_BLE)       stopAP();
  else if (desired == MODE_WIFI) advertising->stop();
  else { if (!wifiApUp) startAP(); advertising->start(); }
  needRedraw = true;
}

void setup() {
  Serial.begin(115200);
  Keyboard.begin();
  USB.begin();

#if HAS_DISPLAY
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  #if USE_ST7735
    tft.initR(INITR_MINI160x80);       // 80x160 panel
    tft.setRotation(3);
    tft.invertDisplay(true);
  #elif USE_ST7789
    tft.init(135, 240);                 // 1.14" 240x135 panel
    tft.setRotation(3);
    // if colours look inverted on hardware, toggle: tft.invertDisplay(false);
  #endif
  tft.fillScreen(ST77XX_BLACK);
#endif

  genCreds();

  WiFi.onEvent(onWiFiEvent);
  startAP();

  BLEDevice::init(apSsid.c_str());
  BLEServer *srv = BLEDevice::createServer();
  srv->setCallbacks(new SrvCb());
  BLEService *svc = srv->createService(SERVICE_UUID);
  BLECharacteristic *rx = svc->createCharacteristic(
      CHAR_RX_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
#if REQUIRE_BLE_PAIRING
  rx->setAccessPermissions(ESP_GATT_PERM_WRITE_ENC_MITM);
#endif
  rx->setCallbacks(new RxCb());
  BLECharacteristic *tx = svc->createCharacteristic(CHAR_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  tx->addDescriptor(new BLE2902());
  svc->start();

  advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->start();

#if REQUIRE_BLE_PAIRING
  BLEDevice::setSecurityCallbacks(new SecCb());
  BLESecurity *sec = new BLESecurity();
  sec->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
  sec->setCapability(ESP_IO_CAP_OUT);
  sec->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  sec->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
#endif

  server.on("/", []() { server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/type", HTTP_POST, []() { Keyboard.print(server.arg("plain")); server.send(200, "text/plain", "ok"); });
  server.on("/key",  HTTP_POST, []() { doKey(server.arg("plain")); server.send(200, "text/plain", "ok"); });
  server.begin();

  current = MODE_IDLE;
  drawScreen();
}

void loop() {
  server.handleClient();
  updateMode();
  if (needRedraw) { needRedraw = false; drawScreen(); }
  delay(10);
}
