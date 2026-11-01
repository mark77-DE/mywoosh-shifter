# MyWoosh Shifter

Kleine Bluetooth-Tastatur (BLE-HID) auf Basis eines ESP32-C3 Super Mini mit vier Tastern zum Schalten und Abbiegen in MyWoosh. Die Tastenbelegung lässt sich per Handy über eine kleine Konfigurationsseite ändern.

| Taster | Pin   | Standardtaste | Funktion         |
|--------|-------|---------------|------------------|
| 1      | GPIO4 | `i`           | hochschalten     |
| 2      | GPIO5 | `k`           | herunterschalten |
| 3      | GPIO6 | Pfeil links   | links abbiegen   |
| 4      | GPIO7 | Pfeil rechts  | rechts abbiegen  |

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
3. In MyWoosh schalten die Taster nun hoch, runter und biegen links bzw. rechts ab (Standardbelegung).

## Status-LED

Die Onboard-LED (GPIO8) zeigt den Zustand an:

| LED                | Bedeutung                            |
|--------------------|--------------------------------------|
| blinkt langsam     | nicht verbunden, wartet auf Kopplung |
| leuchtet dauerhaft | mit dem Gerät verbunden              |
| blinkt schnell     | Konfigurationsmodus (Hotspot aktiv)  |

## Tasten per Handy zuordnen

Funktioniert auf Android und iOS, ohne App.

1. Taster 1 gedrückt halten und gleichzeitig den ESP mit Strom versorgen (z. B. USB einstecken). Nach etwa 1,5 Sekunden startet der Hotspot, die LED blinkt schnell.
2. Mit dem Handy das offene WLAN **MyWoosh-Shifter-Setup** verbinden (kein Passwort).
3. Die Konfigurationsseite öffnet sich meist automatisch. Sonst im Browser `http://192.168.4.1` aufrufen.
4. Für jeden Taster die gewünschte Taste wählen und auf **Speichern & neu starten** tippen.
5. Der ESP startet neu, der Hotspot ist danach wieder aus und die Belegung bleibt gespeichert.

Zur Auswahl stehen Buchstaben, Ziffern, F1 bis F12, Pfeiltasten, Enter, Esc, Leertaste, Tab, Bild auf/ab, Pos1 und Ende. Mit "keine" wird ein Taster deaktiviert.

Im Konfigurationsmodus ist Bluetooth aus, im normalen Betrieb ist WLAN aus. Die bestehende Kopplung mit MyWoosh bleibt davon unberührt.

## Konfiguration im Code

Oben in `src/main.cpp` lassen sich anpassen:

- `PIN_BTN`: GPIO-Pins der vier Taster
- `LED_ACTIVE_LOW`: auf `false` setzen, falls die LED verkehrt herum reagiert
- `BLINK_MS`, `CONFIG_BLINK_MS`: Blinkgeschwindigkeit
- `AP_SSID`: Name des Konfigurations-WLANs
- `CONFIG_HOLD_MS`: wie lange Taster 1 beim Einschalten gehalten werden muss
- `DEFAULT_KEYS`: Standardbelegung, solange noch nichts gespeichert wurde

In der Liste `buttons` legt das Feld `hold` fest, ob eine Taste nur kurz angetippt wird (`false`, Standard) oder gedrückt bleibt, solange der Taster gehalten wird (`true`).

## Hinweise

- Wichtig: Platform und Bibliothek müssen zusammenpassen. In der `platformio.ini` sind `espressif32@6.9.0` und `NimBLE-Arduino@1.4.3` fest gepinnt. Mit der neueren Platform (Arduino-Core 3.x) gibt es viele Build-Warnungen und einen Boot-Loop beim Start von BLE.
- Nach einem Wechsel der Versionen den Ordner `.pio` löschen und neu bauen.
- Android meldet beim Konfigurations-WLAN "Kein Internet". Verbunden bleiben, sonst springt das Handy ins Mobilfunknetz zurück.
- Falls beim Bauen ein Fehler wegen zu großem Programm kommt, in der `platformio.ini` die Zeile `board_build.partitions = huge_app.csv` ergänzen.
- Bei Kopplungsproblemen unter Windows den alten Bluetooth-Eintrag entfernen und neu koppeln.

## Projektstruktur

```
mywoosh-shifter/
├── platformio.ini
├── README.md
└── src/
    └── main.cpp
```