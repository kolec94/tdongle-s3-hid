/*
 * T-Dongle S3 — USB-HID keyboard, dual BLE + WiFi, with on-screen credentials.
 *
 * Plug into the TARGET computer; it enumerates as a USB keyboard. At boot it
 * brings up BOTH a BLE (Nordic UART) service AND a WiFi access point, and shows
 * the WiFi SSID/password and a BLE pairing PIN on the LCD. Connect from a
 * control machine over whichever is convenient:
 *
 *   - WiFi: join the AP, open http://192.168.4.1  (page served by the dongle)
 *   - BLE : use web/control.html (Web Bluetooth), enter the on-screen PIN
 *
 * Whichever transport connects first DISABLES the other until it disconnects,
 * so only one control path is live at a time. The LCD reflects the state and,
 * once a WiFi client is associated, switches to show the IP to browse to.
 *
 * Board:  ESP32S3 Dev Module
 *   USB CDC On Boot : Enabled
 *   USB Mode        : USB-OTG (TinyUSB)
 *   Partition Scheme: 16M Flash (3MB APP/9.9MB FATFS)   [needs BLE+WiFi room]
 *   Flash Size      : 16MB
 *
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
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <esp_mac.h>
#include <esp_random.h>

// ---- options ----
#define REQUIRE_BLE_PAIRING 1   // 1 = require the on-screen PIN; 0 = open (Just Works)

// ---- T-Dongle S3 ST7735 (0.96" 80x160) pins ----
#define TFT_CS    4
#define TFT_DC    2
#define TFT_RST   1
#define TFT_MOSI  3
#define TFT_SCLK  5
#define TFT_BL    38

// ---- Nordic UART Service UUIDs (match web/control.html) ----
#define SERVICE_UUID  "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_RX_UUID  "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_TX_UUID  "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

USBHIDKeyboard Keyboard;
WebServer server(80);
Adafruit_ST7735 tft = Adafruit_ST7735(&SPI, TFT_CS, TFT_DC, TFT_RST);
BLEAdvertising *advertising = nullptr;

String apSsid, apPass;

volatile uint32_t g_pairPin = 0;   // passkey to show during BLE pairing
volatile bool g_showPin   = false; // true while the pairing PIN should be displayed
volatile bool needRedraw  = false; // ask loop() to refresh the LCD

enum Mode { MODE_IDLE, MODE_WIFI, MODE_BLE };
Mode current = MODE_IDLE;

volatile bool bleConnected = false;
volatile int  wifiClients  = 0;
bool wifiApUp = false;

// ---- embedded WiFi control page (dark theme to match the hosted pages) ----
const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>T-Dongle Keyboard</title><style>
body{font-family:system-ui,sans-serif;max-width:640px;margin:24px auto;padding:0 16px;background:#111;color:#eee}
h1{font-size:1.2rem}
textarea{width:100%;height:150px;font-size:16px;padding:10px;box-sizing:border-box;background:#1c1c1c;color:#eee;border:1px solid #444;border-radius:8px}
.row{display:flex;gap:8px;flex-wrap:wrap;margin-top:10px;align-items:center}
button{font-size:15px;padding:10px 16px;border:0;border-radius:8px;cursor:pointer;background:#3b82f6;color:#fff}
button.alt{background:#374151}
label{display:flex;align-items:center;gap:6px;font-size:14px}
#log{margin-top:12px;font-size:13px;color:#9ca3af}
</style></head><body>
<h1>T-Dongle S3 — Remote Keyboard (WiFi)</h1>
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

// ---------- keystrokes ----------
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

// ---------- LCD ----------
void screen(const String &l1, uint16_t c1, const String &l2, const String &l3, const String &l4) {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(c1);          tft.setCursor(2, 2);  tft.println(l1);
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
           "http://" + WiFi.softAPIP().toString(), "BLE disabled");
  } else {
    screen("TDongle-HID : waiting", ST77XX_YELLOW,
           "WiFi: " + apSsid, "Pass: " + apPass, "BLE: PIN shown at pairing");
  }
}

// ---------- credentials ----------
void genCreds() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  char ss[20];
  snprintf(ss, sizeof(ss), "TDongle-%02X%02X", mac[4], mac[5]);
  apSsid = ss;
  static const char cs[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz23456789";
  const int n = sizeof(cs) - 1;
  apPass = "";
  for (int i = 0; i < 8; i++) apPass += cs[esp_random() % n];   // WPA2 needs >= 8 chars
}

// ---------- WiFi AP control ----------
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

// ---------- BLE callbacks ----------
class SrvCb : public BLEServerCallbacks {
  void onConnect(BLEServer *s) override { bleConnected = true; }
  void onDisconnect(BLEServer *s) override { bleConnected = false; }
};
class RxCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    if (g_showPin) { g_showPin = false; needRedraw = true; }   // paired: drop the PIN screen
    String v = c->getValue();
    if (v.length() == 0) return;
    if (v[0] == '\x01') doKey(v.substring(1));
    else                Keyboard.print(v);
  }
};
// DisplayOnly pairing: the stack generates a passkey, we show it on the LCD and
// the control device is prompted to type it in. Only the stack-agnostic callbacks
// are overridden (onAuthenticationComplete differs between Bluedroid and NimBLE).
class SecCb : public BLESecurityCallbacks {
  uint32_t onPassKeyRequest() override { return 0; }
  void onPassKeyNotify(uint32_t pk) override { g_pairPin = pk; g_showPin = true; needRedraw = true; }
  bool onConfirmPIN(uint32_t pk) override { return true; }
  bool onSecurityRequest() override { return true; }
};

// ---------- mode arbitration (run from loop, not from callbacks) ----------
void updateMode() {
  Mode desired = bleConnected ? MODE_BLE : (wifiClients > 0 ? MODE_WIFI : MODE_IDLE);
  if (desired == current) return;
  current = desired;
  if (desired == MODE_BLE) {
    stopAP();                       // exclusive: kill WiFi while BLE is in use
  } else if (desired == MODE_WIFI) {
    advertising->stop();            // exclusive: stop new BLE while WiFi is in use
  } else {                          // back to idle: bring both back
    if (!wifiApUp) startAP();
    advertising->start();
  }
  needRedraw = true;
}

void setup() {
  Keyboard.begin();
  USB.begin();

  // LCD
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);                 // backlight on
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.initR(INITR_MINI160x80);                // 80x160 panel; see note below if garbled
  tft.setRotation(3);                         // landscape 160x80
  tft.invertDisplay(true);                    // these ST7735S panels usually need inversion
  tft.fillScreen(ST77XX_BLACK);

  genCreds();

  // WiFi AP
  WiFi.onEvent(onWiFiEvent);
  startAP();

  // BLE
  BLEDevice::init(apSsid.c_str());
  BLEServer *srv = BLEDevice::createServer();
  srv->setCallbacks(new SrvCb());
  BLEService *svc = srv->createService(SERVICE_UUID);
  BLECharacteristic *rx = svc->createCharacteristic(
      CHAR_RX_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
#if REQUIRE_BLE_PAIRING
  rx->setAccessPermissions(ESP_GATT_PERM_WRITE_ENC_MITM);   // force authenticated pairing
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
  sec->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);   // authenticated + bonded
  sec->setCapability(ESP_IO_CAP_OUT);                          // DisplayOnly: we show the PIN
  sec->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  sec->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
#endif

  // Web server (active whenever the AP is up)
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
