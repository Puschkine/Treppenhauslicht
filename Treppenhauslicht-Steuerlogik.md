# Treppenhauslicht Steuerlogik

## Zielbild
Der ESP32 uebernimmt die zentrale Logik fuer Bewegungsmelder (BWM), Taster und Lampen.
Shelly Geraete melden Events per HTTP an den ESP32. Der ESP32 entscheidet, wann alle Lampen geschaltet werden.

## System und Rollen
- Shelly 102
- Hat Lampe und Taster
- Sendet Taster-Actions an den ESP

- Shelly 103 (2 Kanal)
- Hat BWM an einem konfigurierbaren Eingangskanal (0 oder 1)
- Schaltet zwei Lampenkanaele

- Shelly 104 (2 Kanal)
- Hat BWM an einem konfigurierbaren Eingangskanal (0 oder 1)
- Schaltet zwei Lampenkanaele

- ESP32
- Hat eigenen BWM Eingang
- Fuehrt zentrale Entscheidungslogik aus
- Bietet Weboberflaeche fuer Konfiguration
- Speichert Konfiguration in LittleFS
- Nutzt NTP fuer Uhrzeit und Zeitfensterlogik

## Umgesetzte Logik
### Bewegungslogik
- Quellen: ESP BWM, Shelly 103 BWM, Shelly 104 BWM
- Bei Flankenwechsel senden Shellys den Zustand an den ESP
- ESP fuehrt einen aktiven BWM Zaehler
- Solange mindestens ein BWM aktiv ist, bleiben Lampen an
- Wenn alle BWMs inaktiv sind, wird nach Nachlaufzeit ausgeschaltet
- Default Nachlauf fuer BWM: 60000 ms (1 Minute)
- Plausibilitaetspruefung: wenn ein BWM zu lange auf EIN bleibt, ohne neue Events, setzt der ESP den Zustand automatisch auf AUS (konfigurierbarer Timeout)
- Optionales Shelly Polling: ESP fragt zyklisch die Input-Zustaende von 103/104 ab und korrigiert verlorene EIN/AUS Events

### Tasterlogik (Shelly 102)
- Taster-Events kommen als HTTP Action am ESP an
- Taster ueberschreibt BWM Logik

Kurzer Tastendruck:
- Wenn Licht aus: alle Lampen an, eigener Nachlauf aktiv
- Wenn Licht an: alle Lampen aus
- Default Nachlauf fuer Kurzdruck: 120000 ms (2 Minuten)

Langer Tastendruck:
- Alle Kanaele an (manuelle Dauer-Einschaltung), bis aktiv ausgeschaltet wird

### Zeitfenster fuer BWM Logik
- Zeitfenster ist konfigurierbar (von/bis)
- Innerhalb des Fensters arbeitet BWM Automatik normal
- Ausserhalb des Fensters ist BWM Automatik aus
- Tasterfunktion bleibt immer moeglich

### NTP
- NTP Server konfigurierbar
- GMT Offset und DST Offset konfigurierbar
- Uhrzeit wird fuer Zeitfensterlogik genutzt

## Konfigurierbare Parameter im Webserver
- Shelly 102 IP
- Shelly 103 IP
- Shelly 104 IP
- BWM Kanal fuer Shelly 103 (0 oder 1)
- BWM Kanal fuer Shelly 104 (0 oder 1)
- Lampen Output fuer Shelly 103 (0 oder 1)
- Lampen Output fuer Shelly 104 (0 oder 1)
- BWM Nachlaufzeit in ms
- Kurzdruck Nachlaufzeit in ms
- Plausi Timeout BWM Event in ms
- Shelly Polling aktiv ja/nein
- Shelly Polling Intervall in ms
- ESP BWM Pin
- ESP BWM LED Pin
- ESP BWM invertiert ja/nein
- Zeitfenster aktiv ja/nein
- Startzeit und Endzeit fuer BWM Logik
- NTP Server
- GMT Offset Sekunden
- DST Offset Sekunden

## Sourcecode Beschreibung
Die Firmware ist modular aufgebaut. Der Einstieg liegt in main.cpp, Logik und APIs sind getrennt.

### src/main.cpp
- Initialisiert Serial, WLAN, LittleFS, NTP-Konfiguration und Webserver
- Erstellt die zentralen Objekte:
  - AppConfig cfg
  - LightingController controller
  - AsyncWebServer server
- Verknuepft die Module ueber setupRoutes(...)
- loop() ruft nur noch OTA und controller.loop() auf

### src/app_config.h und src/app_config.cpp
- Definiert die Struktur AppConfig mit allen Default-Werten
- Verwaltet Persistenz nach /config.json in LittleFS
- Funktionen:
  - loadConfig(AppConfig&): liest JSON und uebernimmt Werte
  - saveConfig(const AppConfig&): schreibt aktuelle Werte
  - applyTimeConfig(const AppConfig&): setzt NTP/GMT/DST via configTime(...)
  - hhmmFromMinutes(...): Hilfsfunktion fuer Anzeige und API

### src/lighting_controller.h und src/lighting_controller.cpp
- Enthalten die komplette Schaltlogik
- Aufgaben:
  - Auswertung ESP-BWM inklusive Entprellung
  - Verarbeiten von Shelly-BWM Events (103/104)
  - Tasterlogik kurz/lang
  - Nachlaufsteuerung
  - Zeitfensterpruefung fuer BWM-Automatik
  - Schalten aller Shelly-Relais via HTTP
- Wichtige Methoden:
  - onMotionEvent(...)
  - handleShortPush()
  - handleLongPush()
  - forceOff()
  - onConfigUpdated()

### src/web_api.h und src/web_api.cpp
- Definiert und registriert alle HTTP-Endpunkte
- Liefert JSON fuer Status und Konfiguration
- Nimmt Konfigurationsupdates per POST /api/config entgegen
- Leitet Bewegungs- und Taster-Events an den LightingController weiter

### data/index.html
- Weboberflaeche fuer Konfiguration und Live-Status
- Nutzt die Endpunkte /api/config, /api/state und Event-Testaufrufe
- Dient als Bedienoberflaeche fuer Inbetriebnahme und Wartung

## Webhook Matrix (tatsaechliche URLs)
ESP Zieladresse: 192.168.178.112

| Geraet | Event | Bedingung | HTTP Ziel am ESP | Methode |
|---|---|---|---|---|
| Shelly 103 | BWM EIN | Nur Input 1 wird verwendet | http://192.168.178.112/api/event/motion?node=103&channel=1&state=1 | GET |
| Shelly 103 | BWM AUS | Nur Input 1 wird verwendet | http://192.168.178.112/api/event/motion?node=103&channel=1&state=0 | GET |
| Shelly 104 | BWM EIN | Nur Input 0 wird verwendet | http://192.168.178.112/api/event/motion?node=104&channel=0&state=1 | GET |
| Shelly 104 | BWM AUS | Nur Input 0 wird verwendet | http://192.168.178.112/api/event/motion?node=104&channel=0&state=0 | GET |
| Shelly 102 | Kurzdruck | Action short_push oder short | http://192.168.178.112/api/event/button?type=short | GET |
| Shelly 102 | Langdruck | Action long_push oder long | http://192.168.178.112/api/event/button?type=long | GET |

Hinweise:
- Wenn Shelly Action-Namen abweichen, kann alternativ type=short oder type=long direkt gesendet werden.
- Der ESP ignoriert bei 103 und 104 Events mit falschem channel Parameter.

## Shelly UI Mapping (1:1 Eintrag)
Die genaue Bezeichnung der Menuepunkte kann je nach Shelly Firmware leicht abweichen. Inhaltlich werden die folgenden Webhook-Aufrufe benoetigt.

| Geraet | Bereich in Shelly UI | Trigger/Event | Methode | URL |
|---|---|---|---|---|
| Shelly 103 | Input Aktionen oder Webhook | Input 1 aktiviert (Rising Edge) | GET | http://192.168.178.112/api/event/motion?node=103&channel=1&state=1 |
| Shelly 103 | Input Aktionen oder Webhook | Input 1 deaktiviert (Falling Edge) | GET | http://192.168.178.112/api/event/motion?node=103&channel=1&state=0 |
| Shelly 104 | Input Aktionen oder Webhook | Input 0 aktiviert (Rising Edge) | GET | http://192.168.178.112/api/event/motion?node=104&channel=0&state=1 |
| Shelly 104 | Input Aktionen oder Webhook | Input 0 deaktiviert (Falling Edge) | GET | http://192.168.178.112/api/event/motion?node=104&channel=0&state=0 |
| Shelly 102 | Button Actions oder Input Actions | short_push (oder kurzer Druck) | GET | http://192.168.178.112/api/event/button?type=short |
| Shelly 102 | Button Actions oder Input Actions | long_push (oder langer Druck) | GET | http://192.168.178.112/api/event/button?type=long |

### Checkliste pro Shelly
1. Ziel-URL exakt uebernehmen.
2. HTTP Methode auf GET setzen.
3. Fuer BWM sowohl EIN als auch AUS Event anlegen.
4. Fuer Shelly 103 nur Input 1 verwenden.
5. Fuer Shelly 104 nur Input 0 verwenden.

## API Uebersicht am ESP
- Status:
  - GET /status
  - GET /api/state

- Konfiguration:
  - GET /api/config
  - POST /api/config

- Events:
  - GET /api/event/motion?node=103|104|esp&channel=0|1&state=0|1
  - GET /api/event/button?type=short|long

- Manuell AUS:
  - POST /api/force/off

## Hinweise fuer Inbetriebnahme
1. In den Shelly Webhook/Action Einstellungen die URLs aus der Matrix eintragen.
2. Im ESP Webserver alle IPs und Kanaele korrekt setzen.
3. NTP und Zeitzone passend konfigurieren (Mitteleuropa typischerweise GMT 3600, DST 3600).
4. Funktionstest durchfuehren:
   - BWM EIN an 103/104 pruefen
   - BWM AUS und Nachlauf pruefen
   - Kurzdruck und Langdruck pruefen
   - Zeitfenster ausserhalb und innerhalb pruefen

## Aktuell gesetzte Defaults
- Shelly 102 IP: 192.168.178.102
- Shelly 103 IP: 192.168.178.103
- Shelly 103 BWM Input: 1
- Shelly 103 Lampen Output: 0
- Shelly 104 IP: 192.168.178.104
- Shelly 104 BWM Input: 0
- Shelly 104 Lampen Output: 0
- ESP BWM Pin: 12
- ESP BWM LED Pin: 32
- Plausi Timeout BWM Event: 180000 ms
- Shelly Polling aktiv: true
- Shelly Polling Intervall: 5000 ms
