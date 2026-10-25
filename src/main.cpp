#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

// ---- Pin configuration ----
static const uint8_t PIN_UP    = 4;   // 'i' (shift up)
static const uint8_t PIN_DOWN  = 5;   // 'k' (shift down)
static const uint8_t PIN_LEFT  = 6;   // arrow left (turn left)
static const uint8_t PIN_RIGHT = 7;   // arrow right (turn right)

// ---- HID usage IDs ----
static const uint8_t KEY_I     = 0x0C;
static const uint8_t KEY_K     = 0x0E;
static const uint8_t KEY_RIGHT = 0x4F;
static const uint8_t KEY_LEFT  = 0x50;

static const uint32_t DEBOUNCE_MS = 25;

static const uint8_t  PIN_LED        = 8;     // onboard LED
static const bool     LED_ACTIVE_LOW = true;  // Super Mini LED is on when pin is LOW
static const uint32_t BLINK_MS       = 500;   // blink half-period while not connected

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

struct Button {
  uint8_t  pin;
  uint8_t  keycode;
  bool     hold;        // true: key stays down while the button is held
  bool     lastRaw;
  bool     stable;
  uint32_t lastChange;
};

static Button buttons[] = {
  {PIN_UP,    KEY_I,     false, true, true, 0},
  {PIN_DOWN,  KEY_K,     false, true, true, 0},
  {PIN_LEFT,  KEY_LEFT,  false, true, true, 0},
  {PIN_RIGHT, KEY_RIGHT, false, true, true, 0},
};

void setup() {
  Serial.begin(115200);

  for (auto& b : buttons) pinMode(b.pin, INPUT_PULLUP);

  pinMode(PIN_LED, OUTPUT);
  setLed(false);

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