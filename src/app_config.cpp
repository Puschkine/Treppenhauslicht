#include "app_config.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <IPAddress.h>
#include <Preferences.h>
#include <limits.h>
#include <math.h>
#include <time.h>

#include "debug_log.h"

namespace {

std::vector<LampConfig> legacyLamps(const String &ip102, const String &ip103, const String &ip104,
                                  uint8_t channel103, uint8_t channel104)
{
  std::vector<LampConfig> lamps;
  const String ips[] = {ip102, ip103, ip104};
  const char *names[] = {"Treppe", "Gang hinten", "Gang vorne"};
  for (size_t node = 0; node < 3; ++node) {
    for (uint8_t channel = 0; channel < (node == 0 ? 1 : 2); ++channel) {
      LampConfig lamp;
      lamp.id = "shelly-" + String(102 + node) + "-" + String(channel);
      lamp.name = names[node];
      if (node != 0) lamp.name += " " + String(channel);
      const String base = "http://" + ips[node] + "/rpc/Switch.Set?id=" + String(channel);
      lamp.onUrl = base + "&on=true";
      lamp.offUrl = base + "&on=false";
      lamp.enabled = !ips[node].isEmpty();
      lamp.automation = node == 0 || channel == (node == 1 ? channel103 : channel104);
      lamp.buttonGuard = node == 0;
      if (!lamp.enabled) {
        lamp.onUrl = "";
        lamp.offUrl = "";
      }
      lamps.push_back(lamp);
    }
  }
  return lamps;
}

bool validConfigId(const String &id)
{
  if (id.isEmpty() || id.length() > 48) return false;
  for (size_t i = 0; i < id.length(); ++i) {
    const char c = id[i];
    if (!isalnum((unsigned char)c) && c != '-' && c != '_') return false;
  }
  return true;
}

bool validHttpUrl(const String &url)
{
  if (!url.startsWith("http://") || url.length() > 512) return false;
  for (size_t i = 0; i < url.length(); ++i) {
    if ((uint8_t)url[i] <= 32 || (uint8_t)url[i] >= 127 || url[i] == '#' || url[i] == '@' || url[i] == '\\') return false;
  }
  int end = url.indexOf('/', 7);
  const int query = url.indexOf('?', 7);
  if (end < 0 || (query >= 0 && query < end)) end = query;
  const String authority = end < 0 ? url.substring(7) : url.substring(7, end);
  const int colon = authority.indexOf(':');
  const String host = colon < 0 ? authority : authority.substring(0, colon);
  if (host.isEmpty()) return false;
  for (size_t i = 0; i < host.length(); ++i) {
    const char c = host[i];
    if (!isalnum((unsigned char)c) && c != '.' && c != '-') return false;
  }
  if (colon >= 0) {
    const String port = authority.substring(colon + 1);
    if (port.isEmpty() || port.length() > 5) return false;
    for (size_t i = 0; i < port.length(); ++i) if (!isdigit((unsigned char)port[i])) return false;
    if (port.toInt() < 1 || port.toInt() > 65535) return false;
  }
  return true;
}

String posixOffset(long long utcOffsetSec)
{
  const long long posixOffsetSec = -utcOffsetSec;
  const char sign = posixOffsetSec < 0 ? '-' : '+';
  const unsigned long long magnitude = posixOffsetSec < 0
                                           ? (unsigned long long)(-posixOffsetSec)
                                           : (unsigned long long)posixOffsetSec;
  char value[24];
  snprintf(value, sizeof(value), "%c%llu:%02llu:%02llu",
           sign,
           magnitude / 3600,
           (magnitude % 3600) / 60,
           magnitude % 60);
  return String(value);
}

String buildTimeZone(const AppConfig &cfg)
{
  String timeZone = "STD" + posixOffset(cfg.gmtOffsetSec);
  if (cfg.daylightOffsetSec != 0) {
    timeZone += "DST";
    timeZone += posixOffset((long long)cfg.gmtOffsetSec + cfg.daylightOffsetSec);
    timeZone += ",M3.5.0/2,M10.5.0/3";
  }
  return timeZone;
}

} // namespace

AppConfig::AppConfig()
{
  ButtonSourceConfig source;
  source.name = "Treppen-Shelly";
  source.ip = "192.168.178.102";
  source.buttonGuard = true;
  buttonSources.push_back(source);
  lamps = legacyLamps(source.ip, "192.168.178.103", "192.168.178.104", 0, 0);
  for (uint8_t node : {103, 104}) {
    MotionSensorConfig sensor;
    sensor.id = "bwm-" + String(node);
    sensor.name = node == 103 ? "Gang hinten" : "Gang vorne";
    sensor.pollingEnabled = true;
    sensor.pollHost = "192.168.178." + String(node);
    sensor.pollChannel = node == 103 ? 1 : 0;
    sensor.legacyNode = String(node);
    motionSensors.push_back(sensor);
  }
}

void writeButtonConfig(JsonArray array, const std::vector<ButtonSourceConfig> &sources)
{
  for (const ButtonSourceConfig &source : sources) {
    JsonObject item = array.add<JsonObject>();
    item["name"] = source.name;
    item["ip"] = source.ip;
    item["enabled"] = source.enabled;
    item["buttonGuard"] = source.buttonGuard;
  }
}

bool readButtonConfig(JsonVariantConst value, std::vector<ButtonSourceConfig> &sources, String &error)
{
  if (!value.is<JsonArrayConst>() || value.size() > MAX_BUTTON_SOURCES) {
    error = "buttonSources muss eine Liste mit maximal " + String(MAX_BUTTON_SOURCES) + " Eintraegen sein";
    return false;
  }
  std::vector<ButtonSourceConfig> parsed;
  for (JsonObjectConst item : value.as<JsonArrayConst>()) {
    ButtonSourceConfig source;
    source.name = item["name"].as<String>();
    source.ip = item["ip"].as<String>();
    source.enabled = item["enabled"] | true;
    source.buttonGuard = item["buttonGuard"] | false;
    String name = source.name;
    name.trim();
    IPAddress address;
    if (!item["name"].is<String>() || name.isEmpty() || source.name.length() > 80 ||
        !item["ip"].is<String>() || !address.fromString(source.ip) || address.toString() != source.ip ||
        address[0] == 0 || address[0] >= 224 || source.ip == "255.255.255.255" ||
        (!item["enabled"].isUnbound() && !item["enabled"].is<bool>()) ||
        (!item["buttonGuard"].isUnbound() && !item["buttonGuard"].is<bool>())) {
      error = "Ungueltige Tasterquelle: Name, IPv4-Adresse und Optionen pruefen";
      return false;
    }
    for (const ButtonSourceConfig &existing : parsed) {
      if (existing.ip == source.ip) {
        error = "Doppelte IP-Adresse in den Tasterquellen";
        return false;
      }
    }
    parsed.push_back(source);
  }
  sources = std::move(parsed);
  return true;
}

void writeMotionConfig(JsonArray array, const std::vector<MotionSensorConfig> &sensors)
{
  for (const MotionSensorConfig &sensor : sensors) {
    JsonObject item = array.add<JsonObject>();
    item["id"] = sensor.id;
    item["name"] = sensor.name;
    item["enabled"] = sensor.enabled;
    item["pollingEnabled"] = sensor.pollingEnabled;
    item["pollHost"] = sensor.pollHost;
    item["pollChannel"] = sensor.pollChannel;
    item["legacyNode"] = sensor.legacyNode;
  }
}

bool readMotionConfig(JsonVariantConst value, std::vector<MotionSensorConfig> &sensors, String &error)
{
  if (!value.is<JsonArrayConst>() || value.size() > MAX_MOTION_SENSORS) {
    error = "motionSensors muss eine Liste mit maximal " + String(MAX_MOTION_SENSORS) + " Eintraegen sein";
    return false;
  }
  std::vector<MotionSensorConfig> parsed;
  for (JsonObjectConst item : value.as<JsonArrayConst>()) {
    MotionSensorConfig sensor;
    sensor.id = item["id"].as<String>();
    sensor.name = item["name"].as<String>();
    sensor.enabled = item["enabled"] | true;
    sensor.pollingEnabled = item["pollingEnabled"] | false;
    sensor.pollHost = item["pollHost"] | "";
    sensor.pollChannel = item["pollChannel"] | 0;
    sensor.legacyNode = item["legacyNode"] | "";
    String name = sensor.name;
    name.trim();
    const bool validHost = sensor.pollHost.isEmpty() ? !sensor.pollingEnabled :
                          validHttpUrl("http://" + sensor.pollHost + "/rpc/Input.GetStatus") &&
                          sensor.pollHost.indexOf('/') < 0 && sensor.pollHost.indexOf('?') < 0;
    if (!item["id"].is<String>() || !validConfigId(sensor.id) || sensor.id == "esp" ||
        !item["name"].is<String>() || name.isEmpty() || sensor.name.length() > 80 ||
        (!item["enabled"].isUnbound() && !item["enabled"].is<bool>()) ||
        (!item["pollingEnabled"].isUnbound() && !item["pollingEnabled"].is<bool>()) ||
        (!item["pollHost"].isUnbound() && !item["pollHost"].is<String>()) ||
        !validHost || (!item["pollChannel"].isUnbound() &&
                      (!item["pollChannel"].is<int>() || item["pollChannel"].as<int>() < 0 ||
                       item["pollChannel"].as<int>() > 255)) ||
        (!item["legacyNode"].isUnbound() && !item["legacyNode"].is<String>()) ||
        (!sensor.legacyNode.isEmpty() && sensor.legacyNode != "103" && sensor.legacyNode != "104")) {
      error = "Ungueltiger BWM: ID, Name, Polling-Host, Kanal und Optionen pruefen";
      return false;
    }
    for (const MotionSensorConfig &existing : parsed) {
      if (existing.id == sensor.id || (!sensor.legacyNode.isEmpty() && existing.legacyNode == sensor.legacyNode)) {
        error = "Doppelte BWM-ID oder bisherige Quellenzuordnung";
        return false;
      }
    }
    parsed.push_back(sensor);
  }
  sensors = std::move(parsed);
  return true;
}

void writeLampConfig(JsonArray array, const std::vector<LampConfig> &lamps)
{
  for (const LampConfig &lamp : lamps) {
    JsonObject item = array.add<JsonObject>();
    item["id"] = lamp.id;
    item["name"] = lamp.name;
    item["onUrl"] = lamp.onUrl;
    item["offUrl"] = lamp.offUrl;
    item["enabled"] = lamp.enabled;
    item["automation"] = lamp.automation;
    item["buttonGuard"] = lamp.buttonGuard;
  }
}

bool readLampConfig(JsonVariantConst value, std::vector<LampConfig> &lamps, String &error)
{
  if (!value.is<JsonArrayConst>() || value.size() > MAX_LAMPS) {
    error = "lamps muss eine Liste mit maximal " + String(MAX_LAMPS) + " Eintraegen sein";
    return false;
  }
  std::vector<LampConfig> parsed;
  for (JsonObjectConst item : value.as<JsonArrayConst>()) {
    LampConfig lamp;
    lamp.id = item["id"].as<String>();
    lamp.name = item["name"].as<String>();
    lamp.onUrl = item["onUrl"].as<String>();
    lamp.offUrl = item["offUrl"].as<String>();
    lamp.enabled = item["enabled"] | true;
    lamp.automation = item["automation"] | true;
    lamp.buttonGuard = item["buttonGuard"] | false;
    String trimmedName = lamp.name;
    trimmedName.trim();
    if (!item["id"].is<String>() || !item["name"].is<String>() ||
        !item["onUrl"].is<String>() || !item["offUrl"].is<String>() ||
        !validConfigId(lamp.id) || trimmedName.isEmpty() || lamp.name.length() > 80 ||
        !((!lamp.enabled && lamp.onUrl.isEmpty() && lamp.offUrl.isEmpty()) ||
          (validHttpUrl(lamp.onUrl) && validHttpUrl(lamp.offUrl))) ||
        (!item["enabled"].isNull() && !item["enabled"].is<bool>()) ||
        (!item["automation"].isNull() && !item["automation"].is<bool>()) ||
        (!item["buttonGuard"].isNull() && !item["buttonGuard"].is<bool>())) {
      error = "Ungueltige Lampe: ID, Name, HTTP-URLs und Optionen pruefen";
      return false;
    }
    for (const LampConfig &existing : parsed) {
      if (existing.id == lamp.id) {
        error = "Doppelte Lampen-ID: " + lamp.id;
        return false;
      }
    }
    parsed.push_back(lamp);
  }
  lamps = std::move(parsed);
  return true;
}

void applyTimeConfig(const AppConfig &cfg)
{
  const String timeZone = buildTimeZone(cfg);
  configTzTime(timeZone.c_str(), cfg.ntpServer.c_str());
}

static void writeConfigDocument(JsonDocument &doc, const AppConfig &cfg)
{
  writeButtonConfig(doc["buttonSources"].to<JsonArray>(), cfg.buttonSources);
  writeMotionConfig(doc["motionSensors"].to<JsonArray>(), cfg.motionSensors);
  doc["espMotionEnabled"] = cfg.espMotionEnabled;
  writeLampConfig(doc["lamps"].to<JsonArray>(), cfg.lamps);
  doc["motionOffDelayMs"] = cfg.motionOffDelayMs;
  doc["buttonShortOverrideMs"] = cfg.buttonShortOverrideMs;
  doc["motionEventTimeoutMs"] = cfg.motionEventTimeoutMs;
  doc["shellyPollIntervalMs"] = cfg.shellyPollIntervalMs;
  doc["espMotionPin"] = cfg.espMotionPin;
  doc["espMotionLedPin"] = cfg.espMotionLedPin;
  doc["espMotionInvert"] = cfg.espMotionInvert;
  doc["timeWindowEnabled"] = cfg.timeWindowEnabled;
  doc["automationEnabled"] = cfg.automationEnabled;
  doc["motionStartMin"] = cfg.motionStartMin;
  doc["motionEndMin"] = cfg.motionEndMin;
  doc["windowUseSun"] = cfg.windowUseSun;
  doc["sunOnlineLookup"] = cfg.sunOnlineLookup;
  doc["latitude"] = cfg.latitude;
  doc["longitude"] = cfg.longitude;
  doc["sunriseOffsetMin"] = cfg.sunriseOffsetMin;
  doc["sunsetOffsetMin"] = cfg.sunsetOffsetMin;
  doc["ntpServer"] = cfg.ntpServer;
  doc["gmtOffsetSec"] = cfg.gmtOffsetSec;
  doc["daylightOffsetSec"] = cfg.daylightOffsetSec;
}

bool saveConfig(const AppConfig &cfg)
{
  JsonDocument doc;
  writeConfigDocument(doc, cfg);
  if (doc.overflowed()) {
    appLogf(APP_LOG_LEVEL, "Nicht genug Speicher fuer die Konfiguration");
    return false;
  }
  File f = LittleFS.open("/config.tmp", "w");
  if (!f) {
    appLogf(APP_LOG_LEVEL, "Konnte config.json nicht schreiben");
    return false;
  }
  const size_t expected = measureJson(doc);
  const size_t written = serializeJson(doc, f);
  f.flush();
  const bool complete = written == expected && f.size() == expected;
  f.close();
  if (!complete || !LittleFS.rename("/config.tmp", "/config.json")) {
    appLogf(APP_LOG_LEVEL, "Konfiguration konnte nicht vollstaendig gespeichert werden");
    LittleFS.remove("/config.tmp");
    return false;
  }
  return true;
}

static void readConfigValues(JsonVariantConst doc, AppConfig &cfg)
{
  cfg.espMotionEnabled = doc["espMotionEnabled"] | true;
  cfg.motionOffDelayMs = doc["motionOffDelayMs"] | cfg.motionOffDelayMs;
  cfg.buttonShortOverrideMs = doc["buttonShortOverrideMs"] | cfg.buttonShortOverrideMs;
  cfg.motionEventTimeoutMs = doc["motionEventTimeoutMs"] | cfg.motionEventTimeoutMs;
  cfg.shellyPollIntervalMs = doc["shellyPollIntervalMs"] | cfg.shellyPollIntervalMs;
  cfg.espMotionPin = doc["espMotionPin"] | cfg.espMotionPin;
  cfg.espMotionLedPin = doc["espMotionLedPin"] | cfg.espMotionLedPin;
  cfg.espMotionInvert = doc["espMotionInvert"] | cfg.espMotionInvert;
  cfg.timeWindowEnabled = doc["timeWindowEnabled"] | cfg.timeWindowEnabled;
  cfg.automationEnabled = doc["automationEnabled"] | cfg.automationEnabled;
  cfg.motionStartMin = doc["motionStartMin"] | cfg.motionStartMin;
  cfg.motionEndMin = doc["motionEndMin"] | cfg.motionEndMin;
  cfg.windowUseSun = doc["windowUseSun"] | cfg.windowUseSun;
  cfg.sunOnlineLookup = doc["sunOnlineLookup"] | cfg.sunOnlineLookup;
  cfg.latitude = doc["latitude"] | cfg.latitude;
  cfg.longitude = doc["longitude"] | cfg.longitude;
  cfg.sunriseOffsetMin = doc["sunriseOffsetMin"] | cfg.sunriseOffsetMin;
  cfg.sunsetOffsetMin = doc["sunsetOffsetMin"] | cfg.sunsetOffsetMin;
  cfg.ntpServer = doc["ntpServer"] | cfg.ntpServer;
  cfg.gmtOffsetSec = doc["gmtOffsetSec"] | cfg.gmtOffsetSec;
  cfg.daylightOffsetSec = doc["daylightOffsetSec"] | cfg.daylightOffsetSec;
}

namespace {

constexpr size_t MAX_CONFIG_BACKUP_BYTES = 64 * 1024;
const char *BACKUP_NAMESPACE = "light-backup";
const char *BACKUP_KEY = "config";

bool parseConfigBackup(const String &payload, AppConfig &cfg, uint32_t &createdAt, String &error)
{
  JsonDocument envelope;
  if (deserializeJson(envelope, payload.c_str()) || !envelope["version"].is<unsigned int>() ||
      envelope["version"].as<unsigned int>() != 1 || !envelope["createdAt"].is<uint32_t>() ||
      !envelope["config"].is<JsonObject>()) {
    error = "Preferences-Sicherung ungueltig oder Version nicht unterstuetzt";
    return false;
  }
  JsonVariantConst doc = envelope["config"];
  if (!readButtonConfig(doc["buttonSources"], cfg.buttonSources, error) ||
      !readMotionConfig(doc["motionSensors"], cfg.motionSensors, error) ||
      !readLampConfig(doc["lamps"], cfg.lamps, error)) return false;

  for (const char *key : {"espMotionEnabled", "espMotionInvert", "timeWindowEnabled",
                         "automationEnabled", "windowUseSun", "sunOnlineLookup"}) {
    if (!doc[key].is<bool>()) {
      error = "Ungueltige Sicherung: " + String(key);
      return false;
    }
  }
  struct Range { const char *key; long minimum; long maximum; };
  const Range ranges[] = {
    {"motionOffDelayMs", 100, 600000}, {"buttonShortOverrideMs", 100, 600000},
    {"motionEventTimeoutMs", 1000, 3600000}, {"shellyPollIntervalMs", 1000, 60000},
    {"espMotionPin", 0, 39}, {"espMotionLedPin", 0, 39},
    {"motionStartMin", 0, 1439}, {"motionEndMin", 0, 1439},
    {"sunriseOffsetMin", -180, 180}, {"sunsetOffsetMin", -180, 180},
    {"gmtOffsetSec", LONG_MIN, LONG_MAX}, {"daylightOffsetSec", INT_MIN, INT_MAX}
  };
  for (const Range &range : ranges) {
    if (!doc[range.key].is<long>() || doc[range.key].as<long>() < range.minimum ||
        doc[range.key].as<long>() > range.maximum) {
      error = "Ungueltige Sicherung: " + String(range.key);
      return false;
    }
  }
  if (!doc["latitude"].is<double>() || !doc["longitude"].is<double>() ||
      !isfinite(doc["latitude"].as<double>()) || !isfinite(doc["longitude"].as<double>()) ||
      doc["latitude"].as<double>() < -90 || doc["latitude"].as<double>() > 90 ||
      doc["longitude"].as<double>() < -180 || doc["longitude"].as<double>() > 180 ||
      !doc["ntpServer"].is<String>() || doc["ntpServer"].as<String>().isEmpty()) {
    error = "Ungueltige Sicherung: Standort oder NTP-Server";
    return false;
  }
  readConfigValues(doc, cfg);
  createdAt = envelope["createdAt"].as<uint32_t>();
  return true;
}

bool readConfigBackup(String &payload, String &error)
{
  Preferences preferences;
  if (!preferences.begin(BACKUP_NAMESPACE, false)) {
    error = "Preferences konnten nicht geoeffnet werden";
    return false;
  }
  if (!preferences.isKey(BACKUP_KEY)) {
    preferences.end();
    return true;
  }
  const size_t length = preferences.getBytesLength(BACKUP_KEY);
  if (length == 0 || length > MAX_CONFIG_BACKUP_BYTES) {
    preferences.end();
    error = "Preferences-Sicherung hat eine ungueltige Groesse";
    return false;
  }
  std::vector<char> buffer(length + 1, '\0');
  const size_t read = preferences.getBytes(BACKUP_KEY, buffer.data(), length);
  preferences.end();
  if (read != length || strlen(buffer.data()) != length) {
    error = "Preferences-Sicherung konnte nicht vollstaendig gelesen werden";
    return false;
  }
  payload = buffer.data();
  if (payload.length() != length) {
    error = "Nicht genug Speicher zum Lesen der Preferences-Sicherung";
    return false;
  }
  return true;
}

} // namespace

bool getConfigBackupStatus(size_t &bytes, uint32_t &createdAt, String &error)
{
  bytes = 0;
  createdAt = 0;
  String payload;
  if (!readConfigBackup(payload, error)) return false;
  if (payload.isEmpty()) return true;
  AppConfig parsed;
  if (!parseConfigBackup(payload, parsed, createdAt, error)) return false;
  bytes = payload.length();
  return true;
}

bool backupConfigToPreferences(const AppConfig &cfg, String &error)
{
  JsonDocument config;
  writeConfigDocument(config, cfg);
  JsonDocument envelope;
  envelope["version"] = 1;
  const time_t now = time(nullptr);
  envelope["createdAt"] = now > 1609459200 ? (uint32_t)now : 0;
  envelope["config"] = config.as<JsonObjectConst>();
  String payload;
  const size_t expected = measureJson(envelope);
  if (config.overflowed() || envelope.overflowed() || expected > MAX_CONFIG_BACKUP_BYTES ||
      serializeJson(envelope, payload) != expected || payload.length() != expected) {
    error = "Nicht genug Speicher fuer die Preferences-Sicherung";
    return false;
  }
  AppConfig parsed;
  uint32_t createdAt = 0;
  if (!parseConfigBackup(payload, parsed, createdAt, error)) return false;
  Preferences preferences;
  if (!preferences.begin(BACKUP_NAMESPACE, false)) {
    error = "Preferences konnten nicht geoeffnet werden";
    return false;
  }
  // A single NVS value replaces the previous snapshot atomically.
  const size_t written = preferences.putBytes(BACKUP_KEY, payload.c_str(), payload.length());
  preferences.end();
  String stored;
  const bool verified = written == payload.length() && readConfigBackup(stored, error) && stored == payload;
  if (!verified) {
    error = "Preferences-Sicherung fehlgeschlagen: NVS-Speicher voll oder Schreib-/Lesefehler";
    return false;
  }
  appLogf(APP_LOG_LEVEL, "Konfiguration in Preferences gesichert (%u Bytes)", (unsigned)payload.length());
  return true;
}

bool restoreConfigFromPreferences(AppConfig &restored, String &error)
{
  String payload;
  if (!readConfigBackup(payload, error)) return false;
  if (payload.isEmpty()) {
    error = "Keine Preferences-Sicherung vorhanden";
    return false;
  }
  AppConfig candidate;
  uint32_t createdAt = 0;
  if (!parseConfigBackup(payload, candidate, createdAt, error)) return false;
  if (!saveConfig(candidate)) {
    error = "Sicherung konnte nicht nach LittleFS geschrieben werden";
    return false;
  }
  restored = std::move(candidate);
  appLogf(APP_LOG_LEVEL, "Konfiguration aus Preferences wiederhergestellt");
  return true;
}

bool loadConfig(AppConfig &cfg)
{
  if (!LittleFS.exists("/config.json")) {
    return saveConfig(cfg);
  }

  File f = LittleFS.open("/config.json", "r");
  if (!f) {
    appLogf(APP_LOG_LEVEL, "Konnte config.json nicht lesen");
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    appLogf(APP_LOG_LEVEL, "config.json ungueltig, nutze Defaults");
    return false;
  }

  const String ip102 = doc["shelly102Ip"] | "192.168.178.102";
  const bool migrateButtons = doc["buttonSources"].isUnbound();
  if (migrateButtons) {
    cfg.buttonSources.clear();
    if (!ip102.isEmpty()) {
      ButtonSourceConfig source;
      source.name = "Treppen-Shelly";
      source.ip = ip102;
      source.buttonGuard = true;
      cfg.buttonSources.push_back(source);
    }
  } else {
    String error;
    if (!readButtonConfig(doc["buttonSources"], cfg.buttonSources, error)) {
      appLogf(APP_LOG_LEVEL, "config.json: %s", error.c_str());
      cfg.buttonSources.clear();
      return false;
    }
  }
  const String ip103 = doc["shelly103Ip"] | "192.168.178.103";
  const String ip104 = doc["shelly104Ip"] | "192.168.178.104";
  const bool migrateMotion = doc["motionSensors"].isUnbound();
  if (migrateMotion) {
    for (MotionSensorConfig &sensor : cfg.motionSensors) {
      const bool rear = sensor.legacyNode == "103";
      sensor.pollHost = rear ? ip103 : ip104;
      sensor.pollChannel = rear ? (doc["shelly103MotionChannel"] | 1) : (doc["shelly104MotionChannel"] | 0);
      sensor.enabled = !sensor.pollHost.isEmpty();
      sensor.pollingEnabled = sensor.enabled && (doc["shellyPollingEnabled"] | true);
    }
  } else {
    String error;
    if (!readMotionConfig(doc["motionSensors"], cfg.motionSensors, error)) {
      appLogf(APP_LOG_LEVEL, "config.json: %s", error.c_str());
      cfg.motionSensors.clear();
      return false;
    }
  }
  const bool migrate = doc["lamps"].isUnbound();
  if (migrate) {
    cfg.lamps = legacyLamps(ip102, ip103, ip104,
                            doc["shelly103OutputChannel"] | 0, doc["shelly104OutputChannel"] | 0);
  } else {
    String error;
    if (!readLampConfig(doc["lamps"], cfg.lamps, error)) {
      appLogf(APP_LOG_LEVEL, "config.json: %s", error.c_str());
      cfg.lamps.clear();
      return false;
    }
  }
  readConfigValues(doc.as<JsonVariantConst>(), cfg);
  if (migrate || migrateMotion || migrateButtons) {
    appLogf(APP_LOG_LEVEL, "Konfiguration migriert: Lampen=%s, BWM=%s, Taster=%s",
            migrate ? "ja" : "nein", migrateMotion ? "ja" : "nein", migrateButtons ? "ja" : "nein");
    return saveConfig(cfg);
  }
  return true;
}

String hhmmFromMinutes(uint16_t mins)
{
  uint16_t m = mins % (24 * 60);
  uint8_t h = m / 60;
  uint8_t mm = m % 60;
  char buf[6];
  snprintf(buf, sizeof(buf), "%02u:%02u", h, mm);
  return String(buf);
}