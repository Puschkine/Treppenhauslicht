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
- Quellen: optionaler interner ESP-BWM und frei konfigurierbare externe BWM
- Bei Flankenwechsel senden Shellys den Zustand an den ESP
- ESP speichert pro Melder aktiv/inaktiv und zaehlt aktive, aktivierte Melder;
  wiederholte EIN-/AUS-Meldungen veraendern die Anzahl nicht zusaetzlich
- Solange mindestens ein BWM aktiv ist, bleiben Lampen an
- Wenn alle BWMs inaktiv sind, wird nach Nachlaufzeit ausgeschaltet
- Default Nachlauf fuer BWM: 60000 ms (1 Minute)
- Plausibilitaetspruefung: wenn ein BWM zu lange auf EIN bleibt, ohne neue Events, setzt der ESP den Zustand automatisch auf AUS (konfigurierbarer Timeout)
- Optionales Shelly-Polling je externem Melder korrigiert verlorene EIN/AUS-Events

### Tasterlogik (Shelly 102)
- Taster-Events kommen als HTTP Action am ESP an
- Taster ueberschreibt BWM Logik

Kurzer Tastendruck:
- Wenn Licht aus: alle aktivierten Automatik-Lampen an, eigener Nachlauf aktiv
- Wenn Licht an: alle Lampen aus
- Default Nachlauf fuer Kurzdruck: 120000 ms (2 Minuten)

Langer Tastendruck:
- Alle Kanaele an (manuelle Dauer-Einschaltung), bis aktiv ausgeschaltet wird

### Zeitfenster fuer BWM Logik
- Zeitfenster ist konfigurierbar (von/bis)
- Alternativ kann das Zeitfenster an Sonnenauf-/untergang gekoppelt werden (windowUseSun)
  - Fensterbeginn = Sonnenuntergang - sunsetOffsetMin
  - Fensterende = Sonnenaufgang + sunriseOffsetMin
  - Sonnenzeiten werden bei bestehender WLAN-Verbindung online abgerufen (sunrise-sunset.org API, sunOnlineLookup) und pro Tag zwischengespeichert
  - Ohne Internet oder wenn sunOnlineLookup deaktiviert ist, werden die Sonnenzeiten lokal per Formel aus Latitude/Longitude berechnet
- Innerhalb des Fensters arbeitet BWM Automatik normal
- Ausserhalb des Fensters ist BWM Automatik aus
- Tasterfunktion bleibt immer moeglich

### NTP
- NTP Server konfigurierbar
- GMT Offset und DST Offset konfigurierbar; automatische Sommerzeitumstellung nach EU-Regel (letzter Sonntag im Maerz und Oktober)
- Uhrzeit wird fuer Zeitfensterlogik genutzt

## Konfigurierbare Parameter im Webserver
Die Einstellungsseite beginnt mit drei Geraetelisten: Tasteingaenge, Bewegungsmelder
und Lampen. Jede Liste hat einen Create-Button; die Eintraege oeffnen individuelle
Detailansichten. Der interne BWM ist ein fester, nicht entfernbarer BWM-Eintrag.
Zeiten, Zeitfenster, Polling-Intervall und NTP liegen unter Allgemeine Einstellungen.

Speichern uebernimmt nur den gewaehlten Eintrag, Abbrechen oder Zurueck verwirft
den Entwurf. Create legt erst beim Speichern einen dauerhaften Eintrag an.
Vor dem Speichern wird die aktuelle Konfiguration geladen: andere Eintraege
bleiben erhalten, gleichzeitig geaenderte/entfernte Eintraege werden als Konflikt
gemeldet. Abbrechen und erneutes Oeffnen laedt dann den zuletzt gelesenen Stand.
Speichern initialisiert wie bisher die Steuerung neu und fordert Lampen AUS an.
Lampen-Tests sind nur fuer gespeicherte, aktivierte und unveraenderte Eintraege moeglich.

- Erlaubte Tasterquellen: Name, IPv4-Adresse, aktiviert, Rueckkopplungsschutz
- Internen BWM verwenden ja/nein
- Externe BWM-Liste: Kennung, Name, aktiviert, optional Shelly-Polling, Host und Eingangskanal
- Lampenliste: ID, Name, EIN-/AUS-URL, aktiviert, Automatik und Taster-Rueckkopplungsschutz
- BWM Nachlaufzeit in ms
- Kurzdruck Nachlaufzeit in ms
- Plausi Timeout BWM Event in ms
- Shelly Polling Intervall in ms
- ESP BWM Pin
- ESP BWM LED Pin
- ESP BWM invertiert ja/nein
- Zeitfenster aktiv ja/nein
- Startzeit und Endzeit fuer BWM Logik
- NTP Server
- GMT Offset Sekunden
- DST Offset Sekunden

### Frei konfigurierbare Lampen
- Bis zu 32 Lampen koennen unabhaengig vom Hersteller hinzugefuegt, benannt und entfernt werden.
- Pro Lampe werden zwei explizite HTTP-GET-URLs hinterlegt: `onUrl` und `offUrl`.
  Unterstuetzt werden `http://` mit Hostname oder IPv4 und optionalem Port, ohne
  Anmeldung, Weiterleitungen oder URL-Fragmente; maximal 512 ASCII-Zeichen je URL.
- `enabled` bestimmt, ob eine Lampe geschaltet wird. Deaktivierte Lampen duerfen
  beide URLs leer lassen.
- `automation` bestimmt die Teilnahme an Bewegung und Kurzdruck EIN.
  Langdruck, Alle EIN/AUS, Kurzdruck AUS und Force OFF betreffen alle aktivierten Lampen.
- `buttonGuard` aktiviert fuer Schaltbefehle an den Treppen-Shelly den bestehenden
  Schutz vor Taster-Uebersprechen. Bei dessen Lampe muss die Option aktiviert bleiben.
- Die Tasterquellen-IPs und Polling-Hosts gelten fuer Tasterereignisse und BWM-Polling.
  Aenderungen daran schreiben keine Lampen-URLs um; bei einer IP-Aenderung auch
  die betreffenden EIN-/AUS-URLs aktualisieren.
- Alte Konfigurationen ohne `lamps` werden automatisch in fuenf Lampen ueberfuehrt
  und gespeichert. Die bisherigen Output-Kanaele bestimmen deren Automatik-Option.
  Eine explizit leere Liste bleibt leer.
- Befehle werden in einer Warteschlange nacheinander ausgefuehrt (ein HTTP-Aufruf
  pro loop-Durchlauf, je 700 ms Verbindungs-/Lesetimeout). Noch nicht begonnene
  Befehle derselben Lampe werden durch den neuesten Sollwert ersetzt.
  Startup-AUS wird immer ausgefuehrt, bevor ein spaeterer EIN-Befehl folgt.
- Fehler einer Lampe verhindern keine weiteren Befehle. Es gibt keine automatische
  Wiederholung; ein erneuter EIN-/AUS-Befehl kann manuell gesendet werden.
- Beim Speichern wird neu initialisiert und AUS angefordert. Entfernte, deaktivierte
  oder auf andere URLs umgestellte Lampen erhalten zuvor noch AUS an ihre alte URL.
  Solange Befehle laufen, wird Speichern mit HTTP 409 abgelehnt; danach erneut versuchen.
- Startseite und Einstellungen zeigen je Lampe den ausstehenden bzw. zuletzt
  gesendeten Befehl und HTTP-Fehler, nicht einen bestaetigten Geraetezustand.
- EIN/AUS testen ist nach dem Speichern moeglich und schaltet die echte Lampe,
  ohne den globalen Automatik-/Dauerlichtmodus zu aendern.

API:
- `GET /api/config`: `lamps` und `maxLamps` neben den bisherigen Eingangs-/Zeitparametern.
- `POST /api/config`: validiert die gesamte Lampenliste vor dem atomaren Speichern.
  Der Request darf maximal 64 KiB gross sein.
  IDs sind eindeutig, stabil und enthalten maximal 48 Buchstaben/Ziffern, `-` oder `_`;
  Namen sind maximal 80 UTF-8-Bytes lang.
- `POST /api/manual/relay` mit `{"id":"shelly-102-0","on":true}` oder
  `{"all":true,"on":false}` bestaetigt die Annahme mit HTTP 202, nicht die Ausfuehrung.
  Alte Aufrufe mit `node`/`channel` adressieren weiterhin die migrierten IDs.
- `GET /api/status`: `lamps` mit `id`, `name`, `enabled`, `automation`, `pending`,
  `requestedOn`, optional `lastCommandOn`, `httpCode` und `error`.

### Frei konfigurierbare Bewegungsmelder
- `espMotionEnabled` schaltet den internen BWM ein/aus (Default: ein).
  Ausgeschaltet wird der Eingang nicht gelesen, nicht gezaehlt und die LED ist aus.
  GPIO, Invertierung und LED-Pin bleiben einstellbar. Bei LED-Pinwechsel wird die alte LED ausgeschaltet.
- `motionSensors` enthaelt maximal 32 externe Melder mit `id`, `name`, `enabled`,
  `pollingEnabled`, `pollHost`, `pollChannel` und `legacyNode`.
- Die Oberflaeche erzeugt eine stabile ID; Namen sind frei aenderbar.
  IDs sind eindeutig, maximal 48 ASCII-Buchstaben/Ziffern, `-` oder `_`;
  `esp` ist fuer den lokalen Melder reserviert. Namen sind maximal 80 UTF-8-Bytes lang.
- Jeder Melder sendet EIN und AUS an:
  `http://<ESP-IP>/api/event/motion?id=<id>&state=1` bzw. `state=0`.
  Die Einstellungen zeigen kopierbare URLs passend zur aufgerufenen ESP-Adresse an.
  Ohne sichere Zwischenablage-API wird die URL zum manuellen Kopieren markiert.
- Unbekannte/deaktivierte Melder und fehlende/ungueltige Zustaende werden mit HTTP 400 abgelehnt
  und geloggt. Erlaubte Zustaende: `0`, `1`, `false`, `true`, `off`, `on`.
  `id` hat Vorrang vor den bisherigen Quellenparametern.
- Alte Konfigurationen ohne `motionSensors` werden automatisch und persistent migriert:
  Shelly 103 -> `bwm-103`, Shelly 104 -> `bwm-104`; bisherige IPs, Eingangskanaele
  und globale Polling-Auswahl werden uebernommen. Leere IP deaktiviert den Melder.
  Eine explizit leere Liste bleibt leer; der lokale BWM ist davon unabhaengig.
- `legacyNode` ist fuer uebernommene Melder `103` oder `104` und muss eindeutig sein.
  Neue Melder haben keine alte Zuordnung. Bisherige `node`-, `device`- und
  `sensor`-URLs bleiben erhalten und pruefen weiterhin den konfigurierten Eingangskanal.
  Der alte Aufruf `node=esp` bleibt bei aktiviertem internem BWM moeglich.
- Polling ist je Melder optional und Shelly-spezifisch (`/rpc/Input.GetStatus?id=<kanal>`).
  `pollHost` ist Hostname/IPv4 mit optionalem Port, ohne URL-Pfad oder Anmeldung;
  Eingangskanaele sind 0 bis 255. Ohne Polling darf der Host leer sein.
  `shellyPollIntervalMs` gilt gemeinsam. Pro loop-Durchlauf wird hoechstens ein
  Melder abgefragt; neue HTTP-Events waehrend einer Abfrage werden nicht durch deren Antwort ueberschrieben.
- `motionEventTimeoutMs` gilt weiterhin gemeinsam fuer alle Melder.
  Jede gueltige Meldung/Polling-Antwort erneuert die Plausibilitaetsfrist.
  Timeout setzt nur den betroffenen Melder inaktiv und wird angezeigt und geloggt.
- Der Nachlauf beginnt erst bei null aktiven Meldern; neue Bewegung bricht ihn ab.
  Zeitfenster und manuelle Uebersteuerung bleiben unveraendert.
- `/api/config` liefert `motionSensors`, `espMotionEnabled` und `maxMotionSensors`.
  `/api/status` liefert die externe Liste plus internen Melder mit `enabled`,
  `active`, `local`, `timedOut`, `lastSource`, optional `lastEventAgeMs` und
  optional `pollHealthy`. Alter seit letzter Meldung ist relativ zur ESP-Laufzeit,
  nicht zur Browser-Uhr. Nach Neustart sind externe Zustaende unbekannt/inaktiv, bis
  ein Event oder Polling kommt. Beim Speichern bleiben Zustaende durchgehend aktivierter,
  gleich identifizierter externer Melder erhalten; entfernte/deaktivierte Melder zaehlen nicht mehr.
- Startseite und Einstellungen zeigen alle Melder mit Name, Aktivzustand, letzter Meldung,
  Timeout und Polling-Fehlern. Alte Statusfelder `motionEsp`, `motion103`, `motion104`
  bleiben fuer bestehende Clients erhalten.

### Erweiterbare Tasterquellen
- `buttonSources` enthaelt maximal 32 Quellen mit `name`, `ip`, `enabled` und `buttonGuard`.
  Name: maximal 80 UTF-8-Bytes, nicht leer. IP: eindeutige IPv4-Unicastadresse in
  dezimaler Schreibweise ohne fuehrende Nullen, Hostnamen oder Ports.
- In den Einstellungen koennen Quellen hinzugefuegt, entfernt und deaktiviert werden.
  Neue Quellen sind aktiviert, ihr Rueckkopplungsschutz ist zunaechst aus.
- Alle Quellen verwenden unveraendert `/api/event/button?type=short` und `?type=long`;
  keine ID ist erforderlich. Autorisiert wird die tatsaechliche TCP-Absender-IP,
  nicht ein vom Aufrufer uebermittelter Quellenname.
- Bei Konfigurationen ohne `buttonSources` wird `shelly102Ip` als Treppen-Shelly
  mit aktiviertem Rueckkopplungsschutz uebernommen und persistent migriert.
  Eine bisher leere IP oder eine explizit leere Liste erlaubt keine normalen Tasterquellen.
- `/api/config` liefert `buttonSources` und `maxButtonSources`; POST validiert die
  gesamte Liste, einschliesslich deaktivierter Quellen, vor dem Speichern.
- Nicht erlaubte/deaktivierte Absender werden weiterhin mit HTTP 403 abgelehnt.
- `buttonGuard` aktiviert fuer diese Quelle die bestehende globale 1500-ms-Sperre
  nach Lampenbefehlen, deren Lampenkonfiguration ebenfalls `buttonGuard=true` hat.
  Fuer den Treppen-Shelly beibehalten, fuer unabhaengige Taster ausgeschaltet lassen.
  Dies ist keine zusaetzliche Entprellung und keine individuelle Lampen-Zuordnung.
- Der bestehende explizite Testzugang `source=ui` bleibt unveraendert: er umgeht
  Quellenpruefung und Rueckkopplungsschutz, auch bei leerer Liste.
  Die Quellenliste ist daher keine vollstaendige Authentifizierung.
- Bestehende Taster-Aktionsauswertung, Short-/Long-Verhalten und Lampen-URLs bleiben unveraendert.

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
  - Verarbeiten frei konfigurierbarer BWM-Events und kompatibler alter Shelly-URLs
  - Tasterlogik kurz/lang
  - Nachlaufsteuerung
  - Zeitfensterpruefung fuer BWM-Automatik
  - Schalten frei konfigurierbarer Lampen via HTTP-Warteschlange
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
- Nutzt die Endpunkte /api/config, /api/status und Event-Testaufrufe
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
  - GET /api/status

- Konfiguration:
  - GET /api/config
  - POST /api/config

- Lampensteuerung:
  - POST /api/manual/relay mit `{"all":true,"on":true}` fuer alle Lampen; `"on":false` schaltet sie aus
  - POST /api/manual/relay mit `{"node":103,"channel":1,"on":true}` fuer eine einzelne Lampe; node 102/103/104 und channel 0/1 (Shelly 102 nur Kanal 0), `"on":false` schaltet aus

- Events:
  - GET /api/event/motion?node=103|104|esp&channel=0|1&state=0|1
  - GET /api/event/button?type=short|long

- Manuell AUS:
  - POST /api/force/off

## Hinweise fuer Inbetriebnahme
1. In den Shelly Webhook/Action Einstellungen die URLs aus der Matrix eintragen.
2. Im ESP Webserver alle IPs und Kanaele korrekt setzen.
3. NTP und Zeitzone passend konfigurieren (Mitteleuropa typischerweise GMT 3600, DST 3600; automatische EU-Umstellung).
4. Funktionstest durchfuehren:
   - BWM EIN an 103/104 pruefen
   - BWM AUS und Nachlauf pruefen
   - Kurzdruck und Langdruck pruefen
   - Zeitfenster ausserhalb und innerhalb pruefen

## Aktuell gesetzte Defaults
- Tasterquelle Treppen-Shelly: 192.168.178.102, aktiviert mit Rueckkopplungsschutz
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
