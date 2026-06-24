/*
 * T-Dongle S3 — WiFi-to-USB-HID keyboard
 *
 * Plug the dongle into the TARGET computer's USB port. It enumerates as a
 * USB keyboard AND brings up a WiFi access point with a web page. Connect
 * to that WiFi from another device, type text, and it is injected as
 * keystrokes into the target computer.
 *
 * Board:  ESP32S3 Dev Module  (or "LilyGo T-Dongle-S3" if you have it)
 * Tools:  USB CDC On Boot      -> Enabled
 *         USB Mode             -> USB-OTG (TinyUSB)
 *         Upload Mode          -> UART0 / Hardware CDC
 *         PSRAM                -> OPI PSRAM (T-Dongle S3 has 8MB)
 */

#include <WiFi.h>
#include <WebServer.h>
#include "USB.h"
#include "USBHIDKeyboard.h"

USBHIDKeyboard Keyboard;
WebServer server(80);

// ---- Access point credentials (change these) ----
const char *AP_SSID = "TDongle-HID";
const char *AP_PASS = "type12345";   // must be >= 8 chars

// ---- Web page ----
const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>T-Dongle Keyboard</title>
<style>
  body{font-family:system-ui,sans-serif;max-width:640px;margin:24px auto;padding:0 16px;background:#111;color:#eee}
  h1{font-size:1.2rem}
  textarea{width:100%;height:160px;font-size:16px;padding:10px;box-sizing:border-box;
           background:#1c1c1c;color:#eee;border:1px solid #444;border-radius:8px}
  .row{display:flex;gap:8px;flex-wrap:wrap;margin-top:10px}
  button{font-size:15px;padding:10px 16px;border:0;border-radius:8px;cursor:pointer;
         background:#3b82f6;color:#fff}
  button.alt{background:#374151}
  label{display:flex;align-items:center;gap:6px;font-size:14px}
  #log{margin-top:12px;font-size:13px;color:#9ca3af}
</style>
</head>
<body>
  <h1>T-Dongle S3 — Remote Keyboard</h1>
  <textarea id="txt" placeholder="Type text to send to the target computer..."></textarea>
  <div class="row">
    <button onclick="send()">Send</button>
    <label><input type="checkbox" id="enter" checked> Press Enter after</label>
  </div>
  <div class="row">
    <button class="alt" onclick="key('ENTER')">Enter</button>
    <button class="alt" onclick="key('TAB')">Tab</button>
    <button class="alt" onclick="key('ESC')">Esc</button>
    <button class="alt" onclick="key('WIN')">Win/Cmd</button>
    <button class="alt" onclick="key('CTRL_ALT_DEL')">Ctrl+Alt+Del</button>
  </div>
  <div id="log"></div>
<script>
async function post(path, body){
  const r = await fetch(path, {method:'POST', headers:{'Content-Type':'text/plain'}, body});
  document.getElementById('log').textContent = r.ok ? 'sent ✓' : 'error: '+r.status;
}
function send(){
  let t = document.getElementById('txt').value;
  if(document.getElementById('enter').checked) t += '\n';
  post('/type', t);
}
function key(k){ post('/key', k); }
</script>
</body>
</html>
)HTML";

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

// Type a string of text as keystrokes (US layout)
void handleType() {
  String body = server.arg("plain");
  Keyboard.print(body);          // print() handles \n as Enter
  server.send(200, "text/plain", "ok");
}

// Send a single special key / combo
void handleKey() {
  String k = server.arg("plain");
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
  server.send(200, "text/plain", "ok");
}

void setup() {
  // Start USB HID keyboard
  Keyboard.begin();
  USB.begin();

  // Start WiFi access point
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  server.on("/", handleRoot);
  server.on("/type", HTTP_POST, handleType);
  server.on("/key",  HTTP_POST, handleKey);
  server.begin();
}

void loop() {
  server.handleClient();
}
