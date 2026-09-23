# Requirements Dokument - Treppenhauslicht

## 1. Ziel und Kontext
Dieses Dokument beschreibt die finalen fachlichen und technischen Anforderungen fuer das Projekt Treppenhauslicht.
Der ESP32 ist die zentrale Steuerung fuer Bewegungsmelder, Taster und Lampen auf mehreren Shelly-Geraeten.

## 2. Systemuebersicht
- Zentrale Instanz: ESP32
- Shelly 102: Taster + Lampe
- Shelly 103 (2 Kanal): BWM-Eingang + Lampenausgaenge
- Shelly 104 (2 Kanal): BWM-Eingang + Lampenausgaenge

## 3. Architektur-Anforderungen
### AR-01 Modulare Firmware
Die Firmware muss in Module aufgeteilt sein:
- Konfiguration/Persistenz
- Steuerlogik (State Machine)
- Web/API
- Debug-Logging

### AR-02 Webbasierte Konfiguration
Der ESP32 muss eine Weboberflaeche zur Laufzeit-Konfiguration bereitstellen.

### AR-03 Persistente Konfiguration
Alle Einstellungen muessen persistent in LittleFS als JSON gespeichert werden.

## 4. Kommunikations-Anforderungen
### COM-01 Shelly-Kommunikation via RPC
Die Kommunikation zu Shelly muss ausschliesslich ueber RPC-Endpunkte erfolgen.

### COM-02 Schalten von Relais
Der ESP muss Relais ueber RPC schalten:
- /rpc/Switch.Set?id={kanal}&on={true|false}

### COM-03 Polling von Input-Status
Der ESP muss Shelly-Inputstatus ueber RPC pollen:
- /rpc/Input.GetStatus?id={kanal}

### COM-04 Event-Eingang
Shelly-Ereignisse muessen ueber HTTP-Endpunkte am ESP empfangen werden.

## 5. Funktions-Anforderungen
### FR-01 Bewegungsmelder-Quellen
Die Steuerung beruecksichtigt drei Bewegungsquellen:
- Lokal am ESP
- Shelly 103 (konfigurierbarer BWM-Eingangskanal)
- Shelly 104 (konfigurierbarer BWM-Eingangskanal)

### FR-02 Flankenverarbeitung
BWM-Zustaende werden als EIN/AUS-Flanken verarbeitet.

### FR-03 Aktiv-Zaehler
Die State Machine muss intern die Anzahl aktiver BWM-Quellen verwalten.

### FR-04 Einschaltregel
Solange mindestens ein BWM aktiv ist, bleibt Licht im Automatikbetrieb an.

### FR-05 Nachlaufregel
Der Nachlauf darf erst starten, wenn kein BWM mehr aktiv ist.

### FR-06 Ausschaltregel
Nach Ablauf des BWM-Nachlaufs wird ausgeschaltet, inklusive finaler Plausibilitaetspruefung unmittelbar vor OFF.

### FR-07 Re-Trigger-Guard
Nach automatischem OFF muss eine kurze Re-Trigger-Sperre greifen, um sofortiges Wieder-EIN zu verhindern.

### FR-08 Zeitfenster
BWM-Automatik muss auf ein konfigurierbares Zeitfenster begrenzbar sein.
Ausserhalb des Zeitfensters ist nur manuelle Tastersteuerung aktiv.

### FR-09 Taster Short Push
Short Push muss die BWM-Logik uebersteuern:
- Wenn Licht AUS: EIN fuer den konfigurierten Taster-Nachlauf
- Wenn Licht AN: AUS

### FR-10 Taster Long Push
Long Push schaltet alle Lampenkanaele EIN (Global-ON-Modus).

### FR-11 Prioritaet Manuell
Manuelle Tasteraktionen muessen gegenueber BWM-Automatik Vorrang haben.

### FR-12 Startup-Sicherheit
Beim Hochfahren muessen alle Lampenkanaele explizit AUS geschaltet werden.

### FR-13 Kanalverhalten Laufzeit
Im Automatikbetrieb (BWM/Short Push ON) duerfen nur konfigurierte Lampenausgaenge je Shelly 103/104 geschaltet werden.
Nicht konfigurierte Kanaele duerfen nur bei Long Push eingeschaltet werden.

### FR-14 Globales Ausschalten
Bei Short Push OFF und API Force OFF muessen alle Lampenkanaele ausgeschaltet werden.

## 6. Plausibilitaets- und Robustheits-Anforderungen
### ROB-01 Timeout-basierte Selbstheilung
Wenn ein BWM-Zustand zu lange auf EIN bleibt, ohne neue Events, muss der ESP den Zustand auf AUS ruecksetzen.

### ROB-02 Polling-basierte Korrektur
Der ESP muss zyklisch den Shelly-Inputstatus pollen und lokale BWM-Zustaende korrigieren, falls Event-Kommandos verloren gingen.

### ROB-03 Polling-Fehlerdiagnostik
Fehlgeschlagene Polling-Vorgaenge muessen als Debug-Ereignisse sichtbar sein.

### ROB-04 Polling-Recovery
Wiederherstellung nach Polling-Fehler muss erkannt und geloggt werden.

## 7. Sicherheits- und Plausibilitaetsregeln fuer API
### API-01 Button-Quelle
Button-Events duerfen nur von Shelly 102 oder explizit als UI-Testquelle akzeptiert werden.

### API-02 Button-Action-Validierung
Nur explizite Short- oder Long-Buttonaktionen duerfen ausgefuehrt werden.
Unbekannte/fehlende Actions muessen verworfen werden.

### API-03 Channel-Filter
BWM-Events fuer Shelly 103/104 muessen den konfigurierten BWM-Eingangskanal treffen, sonst werden sie ignoriert.

## 8. Debug- und Diagnose-Anforderungen
### DBG-01 Konsolidiertes Logging
System- und Steuerlogs muessen in einem zentralen In-Memory-Log gesammelt werden.

### DBG-02 Web-Loganzeige
Debuglogs muessen auf der Weboberflaeche angezeigt werden.

### DBG-03 Log-Aktionen
Das Log muss aktualisierbar und loeschbar sein.

### DBG-04 Ereignisnachvollziehbarkeit
Logs muessen mindestens enthalten:
- Quelle von BWM-Events (lokal/103/104)
- Eventstatus EIN/AUS
- Nachlaufstart
- Schaltgrund fuer Relais AN/AUS
- Button-Request-Herkunft

## 9. Zeit-/NTP-Anforderungen
### TIME-01 NTP
NTP-Server muss konfigurierbar sein.

### TIME-02 Zeitzone
GMT-Offset und DST-Offset muessen konfigurierbar sein.

### TIME-03 Nutzung der Uhrzeit
Zeitdaten muessen fuer das BWM-Zeitfenster verwendet werden.

## 10. Konfigurations-Anforderungen
### CFG-01 Pflichtparameter
Konfigurierbare Parameter:
- shelly102Ip
- shelly103Ip
- shelly104Ip
- shelly103MotionChannel
- shelly104MotionChannel
- shelly103OutputChannel
- shelly104OutputChannel
- motionOffDelayMs
- buttonShortOverrideMs
- motionEventTimeoutMs
- shellyPollingEnabled
- shellyPollIntervalMs
- espMotionPin
- espMotionLedPin
- espMotionInvert
- timeWindowEnabled
- motionStartMin
- motionEndMin
- ntpServer
- gmtOffsetSec
- daylightOffsetSec

### CFG-02 Defaultwerte
Defaultwerte:
- shelly102Ip = 192.168.178.102
- shelly103Ip = 192.168.178.103
- shelly104Ip = 192.168.178.104
- shelly103MotionChannel = 1
- shelly104MotionChannel = 0
- shelly103OutputChannel = 0
- shelly104OutputChannel = 0
- motionOffDelayMs = 60000
- buttonShortOverrideMs = 120000
- motionEventTimeoutMs = 180000
- shellyPollingEnabled = true
- shellyPollIntervalMs = 5000
- espMotionPin = 12
- espMotionLedPin = 32
- espMotionInvert = false
- timeWindowEnabled = false
- motionStartMin = 0
- motionEndMin = 0
- ntpServer = pool.ntp.org
- gmtOffsetSec = 3600
- daylightOffsetSec = 3600

## 11. API-Anforderungen
### API-Status
- GET /status
- GET /api/state

### API-Config
- GET /api/config
- POST /api/config

### API-Events
- GET /api/event/motion?node=103|104|esp&channel=0|1&state=0|1
- GET /api/event/button?type=short|long

### API-Steuerung
- POST /api/force/off

### API-Debug
- GET /api/logs
- POST /api/logs/clear

## 12. Webhook-Anforderungen (Soll)
### WH-01 Shelly 103 BWM
- EIN: /api/event/motion?node=103&channel={konfigurierter Input}&state=1
- AUS: /api/event/motion?node=103&channel={konfigurierter Input}&state=0

### WH-02 Shelly 104 BWM
- EIN: /api/event/motion?node=104&channel={konfigurierter Input}&state=1
- AUS: /api/event/motion?node=104&channel={konfigurierter Input}&state=0

### WH-03 Shelly 102 Taster
- Short: /api/event/button?type=short
- Long: /api/event/button?type=long

## 13. Akzeptanzkriterien
### AC-01 Startup
Nach Neustart sind alle Lampenkanaele AUS.

### AC-02 Automatik-Schalten
BWM oder Short Push ON schaltet nur konfigurierte Outputs von 103/104.

### AC-03 Long Push
Long Push schaltet alle Lampenkanaele EIN.

### AC-04 Nachlauf
Nachlauf startet erst wenn alle BWM AUS sind.

### AC-05 Robustheit bei Paketverlust
Verlorene Events werden durch Polling und Timeout-Plausibilitaet korrigiert.

### AC-06 Debugbarkeit
Alle relevanten Ursachen und Schaltgruende sind in Web-Debuglogs sichtbar.

### AC-07 Persistenz
Geaenderte Konfiguration bleibt ueber Neustarts erhalten.
