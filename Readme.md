# MyWoosh Shifter

Kleine Bluetooth-Tastatur (BLE-HID) auf Basis eines ESP32-C3 Super Mini mit vier Tastern zum Schalten und Abbiegen in MyWoosh.

| Taster | Pin   | Taste       | Funktion         |
|--------|-------|-------------|------------------|
| 1      | GPIO4 | `i`         | hochschalten     |
| 2      | GPIO5 | `k`         | herunterschalten |
| 3      | GPIO6 | Pfeil links | links abbiegen   |
| 4      | GPIO7 | Pfeil rechts | rechts abbiegen |

## Hardware

- ESP32-C3 Super Mini (Bluetooth 5 LE)
- 4 Taster

## Verdrahtung

Jeder Taster wird zwischen seinen GPIO-Pin und GND geschaltet. Externe Widerstände sind nicht nötig, der interne Pull-up wird verwendet.

```
GPIO4 ──[Taster 1]── GND
GPIO5 ──[Taster 2]── GND
GPIO6 ──[Taster 3]── GND
GPIO7 ──[Taster 4]── GND
```

## Build & Upload

Voraussetzung: [PlatformIO](https://platformio.org/) (VS Code Extension oder CLI).

```bash
pio run -t upload     # build and flash
pio device monitor    # serial output (115200 baud)
```

Falls der Upload nicht startet: BOOT gedrückt halten, dabei Reset drücken und erneut flashen.

## Koppeln

1. Auf dem Gerät mit MyWoosh (PC, Tablet, Handy, Apple TV) die Bluetooth-Einstellungen öffnen.
2. Nach **"MyWoosh Shifter"** suchen und koppeln.
3. In MyWoosh schalten die Taster nun hoch (`i`), runter (`k`) und biegen links bzw. rechts ab (Pfeiltasten).

## Status-LED

Die Onboard-LED (GPIO8) zeigt den Bluetooth-Status an:

| LED          | Bedeutung                              |
|--------------|----------------------------------------|
| blinkt       | nicht verbunden, wartet auf Kopplung   |
| leuchtet dauerhaft | mit dem Gerät verbunden          |

## Konfiguration

Oben in `src/main.cpp` lassen sich Pins, Tasten und Gerätename anpassen. Für die Status-LED gibt es `LED_ACTIVE_LOW` (auf `false` setzen, falls die LED verkehrt herum reagiert) und `BLINK_MS` für die Blinkgeschwindigkeit. In der Liste `buttons` legt das Feld `hold` fest, ob eine Taste nur kurz angetippt wird (`false`, Standard) oder gedrückt bleibt, solange der Taster gehalten wird (`true`).

## Hinweise

- Wichtig: Platform und Bibliothek müssen zusammenpassen. In der `platformio.ini` sind `espressif32@6.9.0` und `NimBLE-Arduino@1.4.3` fest gepinnt. Mit der neueren Platform (Arduino-Core 3.x) gibt es viele Build-Warnungen und einen Boot-Loop beim Start von BLE.
- Nach einem Wechsel der Versionen den Ordner `.pio` löschen und neu bauen.
- Bei Kopplungsproblemen unter Windows den alten Bluetooth-Eintrag entfernen und neu koppeln.

## Projektstruktur

```
mywoosh-shifter/
├── platformio.ini
├── README.md
└── src/
    └── main.cpp
```