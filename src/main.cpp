#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

// ---- Pin configuration ----
static const uint8_t PIN_BTN[4] = {4, 5, 6, 7};  // buttons 1..4 (to GND, internal pull-up)

// ---- Status LED ----
static const uint8_t  PIN_LED         = 8;     // onboard LED
static const bool     LED_ACTIVE_LOW  = true;  // Super Mini LED is on when pin is LOW
static const uint32_t BLINK_MS        = 500;   // blink half-period while not connected
static const uint32_t CONFIG_BLINK_MS = 150;   // fast blink in config mode

// ---- Config mode ----
static const char*    AP_SSID        = "MyWoosh-Shifter-Setup";  // open network, no password
static const uint32_t CONFIG_HOLD_MS = 1500;  // hold button 1 at power-on for this long

// ---- HID usage IDs (defaults) ----
static const uint8_t KEY_I     = 0x0C;
static const uint8_t KEY_K     = 0x0E;
static const uint8_t KEY_RIGHT = 0x4F;
static const uint8_t KEY_LEFT  = 0x50;
static const uint8_t DEFAULT_KEYS[4] = {KEY_I, KEY_K, KEY_LEFT, KEY_RIGHT};

static const uint32_t DEBOUNCE_MS = 25;

// Keys offered on the config page besides letters, digits and F-keys
struct KeyOpt {
  uint8_t     code;
  const char* name;
};
static const KeyOpt SPECIAL_KEYS[] = {
  {0x52, "Pfeil hoch"},   {0x51, "Pfeil runter"},
  {0x50, "Pfeil links"},  {0x4F, "Pfeil rechts"},
  {0x28, "Enter"},        {0x29, "Esc"},
  {0x2A, "Backspace"},    {0x2B, "Tab"},
  {0x2C, "Leertaste"},    {0x4B, "Bild hoch"},
  {0x4E, "Bild runter"},  {0x4A, "Pos1"},
  {0x4D, "Ende"},
};

// Minimal HID keyboard report descriptor (report ID 1)
static const uint8_t REPORT_MAP[] = {
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,  // Keyboard, Report ID 1
  0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7,              // Modifier keys
  0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08,
  0x81, 0x02,                                      // Modifier byte
  0x95, 0x01, 0x75, 0x08, 0x81, 0x01,              // Reserved byte
  0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
  0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,  // 6 key slots
  0xC0
};

static NimBLEServer*         server = nullptr;
static NimBLEHIDDevice*      hid    = nullptr;
static NimBLECharacteristic* input  = nullptr;
static volatile bool         connected = false;

static Preferences prefs;
static WebServer   web(80);
static DNSServer   dns;
static uint32_t    restartAt = 0;

// ---------------------------------------------------------------------------
// LED
// ---------------------------------------------------------------------------
static void setLed(bool on) {
  digitalWrite(PIN_LED, (on == LED_ACTIVE_LOW) ? LOW : HIGH);
}

// Blink while disconnected, solid while connected (non-blocking)
static void updateLed(uint32_t now) {
  if (connected) {
    setLed(true);
  } else {
    setLed((now / BLINK_MS) % 2 == 0);
  }
}

// ---------------------------------------------------------------------------
// BLE
// ---------------------------------------------------------------------------
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*) override {
    connected = true;
    Serial.println("BLE connected");
  }
  void onDisconnect(NimBLEServer*) override {
    connected = false;
    Serial.println("BLE disconnected, advertising again");
    NimBLEDevice::startAdvertising();
  }
};

// Send a keyboard report with one key (0 = all keys released)
static void sendReport(uint8_t keycode) {
  if (!connected) return;
  uint8_t report[8] = {0, 0, keycode, 0, 0, 0, 0, 0};
  input->setValue(report, sizeof(report));
  input->notify();
}

// Short tap: press, then release
static void sendTap(uint8_t keycode) {
  sendReport(keycode);
  delay(15);
  sendReport(0);
}

// ---------------------------------------------------------------------------
// Buttons and stored key mapping
// ---------------------------------------------------------------------------
struct Button {
  uint8_t  pin;
  uint8_t  keycode;     // loaded from flash, 0 = disabled
  bool     hold;        // true: key stays down while the button is held
  bool     lastRaw;
  bool     stable;
  uint32_t lastChange;
};

static Button buttons[4] = {
  {PIN_BTN[0], 0, false, true, true, 0},
  {PIN_BTN[1], 0, false, true, true, 0},
  {PIN_BTN[2], 0, false, true, true, 0},
  {PIN_BTN[3], 0, false, true, true, 0},
};

// NVS key name for button i: "b0".."b3"
static void keyName(uint8_t i, char* out) {
  out[0] = 'b';
  out[1] = '0' + i;
  out[2] = 0;
}

static bool isValidKey(uint8_t c) {
  if (c == 0) return true;                    // disabled
  if (c >= 0x04 && c <= 0x27) return true;    // letters and digits
  if (c >= 0x3A && c <= 0x45) return true;    // F1..F12
  for (const auto& k : SPECIAL_KEYS) {
    if (k.code == c) return true;
  }
  return false;
}

static void loadKeys() {
  prefs.begin("keys", false);
  for (uint8_t i = 0; i < 4; i++) {
    char name[3];
    keyName(i, name);
    uint8_t v = prefs.getUChar(name, DEFAULT_KEYS[i]);
    buttons[i].keycode = isValidKey(v) ? v : DEFAULT_KEYS[i];
  }
  prefs.end();
}

// ---------------------------------------------------------------------------
// Config page
// ---------------------------------------------------------------------------
static const char PAGE_HEAD[] PROGMEM = R"HTML(<!doctype html>
<html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MyWoosh Shifter</title>
<style>
:root{color-scheme:light dark;--bg:#f4f5f7;--card:#fff;--fg:#1c1e21;--muted:#6b7280;--accent:#2563eb;--border:#d7dbe0}
@media(prefers-color-scheme:dark){:root{--bg:#111318;--card:#1b1e25;--fg:#e8eaed;--muted:#9aa0a6;--accent:#4c8dff;--border:#2d313a}}
body{margin:0;padding:20px;background:var(--bg);color:var(--fg);font:16px/1.4 system-ui,-apple-system,sans-serif}
.wrap{max-width:420px;margin:0 auto}
h1{font-size:1.4rem;margin:8px 0 4px}
.sub{color:var(--muted);margin:0 0 20px;font-size:.95rem}
.card{background:var(--card);border:1px solid var(--border);border-radius:14px;padding:14px 16px;margin-bottom:12px}
label{display:block;font-weight:600;margin-bottom:8px}
label small{color:var(--muted);font-weight:400;margin-left:6px}
select{width:100%;padding:12px;font-size:1rem;border-radius:10px;border:1px solid var(--border);background:var(--bg);color:var(--fg)}
button{width:100%;padding:15px;font-size:1rem;font-weight:600;border:0;border-radius:12px;background:var(--accent);color:#fff;margin-top:6px}
.hint{color:var(--muted);font-size:.85rem;text-align:center;margin-top:14px}
</style></head><body>)HTML";

static String optionsHtml(uint8_t selected) {
  String s;
  auto add = [&](uint8_t code, const String& name) {
    s += "<option value='" + String((int)code) + "'";
    if (code == selected) s += " selected";
    s += ">" + name + "</option>";
  };

  add(0, "- keine -");

  s += "<optgroup label='Pfeile und Sonstige'>";
  for (const auto& k : SPECIAL_KEYS) add(k.code, k.name);

  s += "</optgroup><optgroup label='Buchstaben'>";
  for (uint8_t c = 0; c < 26; c++) add(0x04 + c, String((char)('A' + c)));

  s += "</optgroup><optgroup label='Ziffern'>";
  for (uint8_t d = 1; d <= 9; d++) add(0x1E + d - 1, String((int)d));
  add(0x27, "0");

  s += "</optgroup><optgroup label='Funktionstasten'>";
  for (uint8_t f = 0; f < 12; f++) add(0x3A + f, "F" + String((int)f + 1));
  s += "</optgroup>";
  return s;
}

static void handleRoot() {
  String p = PAGE_HEAD;
  p += "<div class='wrap'><h1>MyWoosh Shifter</h1>"
       "<p class='sub'>Welche Taste soll jeder Taster senden?</p>"
       "<form method='POST' action='/save'>";
  for (uint8_t i = 0; i < 4; i++) {
    p += "<div class='card'><label>Taster " + String((int)i + 1) +
         "<small>GPIO" + String((int)PIN_BTN[i]) + "</small></label>"
         "<select name='b" + String((int)i) + "'>" +
         optionsHtml(buttons[i].keycode) + "</select></div>";
  }
  p += "<button type='submit'>Speichern &amp; neu starten</button></form>"
       "<p class='hint'>Nach dem Speichern startet das Ger&auml;t neu und der Hotspot ist wieder aus.</p>"
       "</div></body></html>";
  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "text/html; charset=utf-8", p);
}

static void handleSave() {
  prefs.begin("keys", false);
  for (uint8_t i = 0; i < 4; i++) {
    char name[3];
    keyName(i, name);
    if (!web.hasArg(name)) continue;
    long v = web.arg(name).toInt();
    if (v >= 0 && v <= 255 && isValidKey((uint8_t)v)) {
      prefs.putUChar(name, (uint8_t)v);
    }
  }
  prefs.end();

  String p = PAGE_HEAD;
  p += "<div class='wrap'><h1>Gespeichert</h1>"
       "<p class='sub'>Das Ger&auml;t startet jetzt neu. Danach ist der Hotspot aus und du kannst dich per Bluetooth verbinden.</p>"
       "</div></body></html>";
  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "text/html; charset=utf-8", p);

  restartAt = millis() + 1500;  // give the browser time to receive the page
}

// Captive portal: send everything else to the config page
static void handleRedirect() {
  web.sendHeader("Location", "http://192.168.4.1/", true);
  web.send(302, "text/plain", "");
}

// Open hotspot with the config page. Never returns, ends with a reboot.
static void runConfigMode() {
  Serial.println("Config mode: hotspot without password");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);                // open network
  WiFi.setTxPower(WIFI_POWER_8_5dBm);  // workaround often needed on Super Mini boards
  delay(100);

  dns.start(53, "*", WiFi.softAPIP());  // answer every DNS request with our IP

  web.on("/", HTTP_GET, handleRoot);
  web.on("/save", HTTP_POST, handleSave);
  web.onNotFound(handleRedirect);
  web.begin();

  Serial.print("SSID: ");
  Serial.print(AP_SSID);
  Serial.print("  IP: ");
  Serial.println(WiFi.softAPIP());

  while (true) {
    dns.processNextRequest();
    web.handleClient();
    setLed((millis() / CONFIG_BLINK_MS) % 2 == 0);
    if (restartAt != 0 && (int32_t)(millis() - restartAt) >= 0) {
      ESP.restart();
    }
    delay(2);
  }
}

// True if button 1 is held for CONFIG_HOLD_MS right after power-on
static bool wantConfigMode() {
  delay(20);  // let the pull-up settle
  uint32_t t0 = millis();
  while (millis() - t0 < CONFIG_HOLD_MS) {
    if (digitalRead(buttons[0].pin)) return false;
    delay(10);
  }
  return true;
}

// ---------------------------------------------------------------------------
// Setup / loop
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  pinMode(PIN_LED, OUTPUT);
  setLed(false);
  for (auto& b : buttons) pinMode(b.pin, INPUT_PULLUP);

  loadKeys();

  if (wantConfigMode()) runConfigMode();  // does not return

  NimBLEDevice::init("MyWoosh Shifter");
  NimBLEDevice::setSecurityAuth(true, false, true);  // bonding, no MITM, secure connections

  server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  hid   = new NimBLEHIDDevice(server);
  input = hid->inputReport(1);

  hid->manufacturer()->setValue("DIY");
  hid->pnp(0x02, 0x05AC, 0x820A, 0x0210);
  hid->hidInfo(0x00, 0x01);
  hid->reportMap((uint8_t*)REPORT_MAP, sizeof(REPORT_MAP));
  hid->startServices();
  hid->setBatteryLevel(100);

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setAppearance(0x03C1);  // HID keyboard
  adv->addServiceUUID(hid->hidService()->getUUID());
  adv->start();

  Serial.println("Advertising as 'MyWoosh Shifter'");
}

void loop() {
  uint32_t now = millis();
  updateLed(now);

  for (auto& b : buttons) {
    if (b.keycode == 0) continue;  // button disabled in config

    bool raw = digitalRead(b.pin);  // LOW = pressed
    if (raw != b.lastRaw) {
      b.lastRaw = raw;
      b.lastChange = now;
    }
    if ((now - b.lastChange) > DEBOUNCE_MS && raw != b.stable) {
      b.stable = raw;
      if (!b.stable) {                 // button pressed
        if (b.hold) sendReport(b.keycode);
        else        sendTap(b.keycode);
      } else if (b.hold) {             // button released
        sendReport(0);
      }
    }
  }
  delay(1);
}