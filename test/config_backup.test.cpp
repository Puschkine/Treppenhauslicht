#define ARDUINOJSON_ENABLE_ARDUINO_STRING 0
#define ARDUINOJSON_ENABLE_ARDUINO_STREAM 0
#define ARDUINOJSON_ENABLE_ARDUINO_PRINT 0
#include <ArduinoJson.h>
#include <cassert>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>
#include <ctime>

struct String : std::string {
  using std::string::string;
  using std::string::operator=;
  String() = default;
  String(const std::string &value) : std::string(value) {}
  template <typename T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>
  explicit String(T value) : std::string(std::to_string(value)) {}
  bool isEmpty() const { return empty(); }
  bool startsWith(const char *prefix) const { return find(prefix) == 0; }
  int indexOf(char c, size_t start = 0) const {
    const auto position = find(c, start);
    return position == npos ? -1 : static_cast<int>(position);
  }
  String substring(size_t start) const { return substr(start); }
  String substring(size_t start, size_t end) const { return substr(start, end - start); }
  long toInt() const { return std::strtol(c_str(), nullptr, 10); }
  void trim() {
    const auto begin = find_first_not_of(" \t\r\n");
    *this = begin == npos ? "" : substr(begin, find_last_not_of(" \t\r\n") - begin + 1);
  }
  size_t write(uint8_t c) { push_back(static_cast<char>(c)); return 1; }
  size_t write(const uint8_t *bytes, size_t length) { append(reinterpret_cast<const char *>(bytes), length); return length; }
};

namespace ArduinoJson {
template <> struct Converter<String> {
  static void toJson(const String &value, JsonVariant destination) { destination.set(value.c_str()); }
  static String fromJson(JsonVariantConst value) { return value.as<std::string>(); }
  static bool checkJson(JsonVariantConst value) { return value.is<const char *>(); }
};
}

static std::map<std::string, std::shared_ptr<std::string>> files;
static bool fileWriteFails, renameFails;
struct File {
  std::shared_ptr<std::string> content;
  size_t position = 0;
  explicit operator bool() const { return bool(content); }
  int read() { return position < content->size() ? static_cast<unsigned char>((*content)[position++]) : -1; }
  size_t readBytes(char *buffer, size_t length) {
    const size_t count = std::min(length, content->size() - position);
    std::memcpy(buffer, content->data() + position, count); position += count; return count;
  }
  size_t write(uint8_t c) { content->push_back(static_cast<char>(c)); return 1; }
  size_t write(const uint8_t *buffer, size_t length) { content->append(reinterpret_cast<const char *>(buffer), length); return length; }
  void flush() {}
  void close() {}
  size_t size() const { return content->size(); }
};
static struct {
  bool exists(const char *path) { return files.count(path); }
  File open(const char *path, const char *mode) {
    if (*mode == 'w') {
      if (fileWriteFails) return {};
      files[path] = std::make_shared<std::string>();
    }
    if (!files.count(path)) return {};
    File result; result.content = files[path]; return result;
  }
  bool rename(const char *from, const char *to) {
    if (renameFails) return false;
    files[to] = files.at(from); files.erase(from); return true;
  }
  void remove(const char *path) { files.erase(path); }
} LittleFS;

struct IPAddress {
  unsigned int bytes[4]{};
  bool fromString(const String &value) {
    char trailing;
    return std::sscanf(value.c_str(), "%u.%u.%u.%u%c", &bytes[0], &bytes[1], &bytes[2], &bytes[3], &trailing) == 4 &&
      bytes[0] <= 255 && bytes[1] <= 255 && bytes[2] <= 255 && bytes[3] <= 255;
  }
  String toString() const {
    return String(bytes[0]) + "." + String(bytes[1]) + "." + String(bytes[2]) + "." + String(bytes[3]);
  }
  unsigned int operator[](size_t index) const { return bytes[index]; }
};

static std::string nvsBlob;
static bool nvsExists, nvsOpenFails, nvsWriteFails, nvsReadFails;
static unsigned int nvsWrites;
struct Preferences {
  bool begin(const char *, bool) { return !nvsOpenFails; }
  bool isKey(const char *) { return nvsExists; }
  size_t getBytesLength(const char *) { return nvsBlob.size(); }
  size_t getBytes(const char *, void *destination, size_t length) {
    if (nvsReadFails) return 0;
    std::memcpy(destination, nvsBlob.data(), length); return length;
  }
  size_t putBytes(const char *, const void *value, size_t length) {
    ++nvsWrites;
    if (nvsWriteFails) return 0;
    nvsBlob.assign(static_cast<const char *>(value), length); nvsExists = true; return length;
  }
  void end() {}
};
static const int APP_LOG_LEVEL = 0;
static void appLogf(int, const char *, ...) {}
static void configTzTime(const char *, const char *) {}

#include "config_test.inc"

static std::string snapshot(const AppConfig &config) {
  JsonDocument doc;
  writeConfigDocument(doc, config);
  std::string result;
  serializeJson(doc, result);
  return result;
}

int main() {
  static_assert(sizeof(long) == 4, "Use a Windows target matching ESP32 integer widths.");
  AppConfig original;
  original.motionOffDelayMs = 32100;
  original.buttonShortOverrideMs = 54300;
  original.motionEventTimeoutMs = 234000;
  original.shellyPollIntervalMs = 7500;
  original.espMotionPin = 13;
  original.espMotionLedPin = 33;
  original.espMotionInvert = true;
  original.timeWindowEnabled = true;
  original.automationEnabled = false;
  original.motionStartMin = 600;
  original.motionEndMin = 720;
  original.windowUseSun = true;
  original.sunOnlineLookup = false;
  original.latitude = -40.5;
  original.longitude = 120.25;
  original.sunriseOffsetMin = -45;
  original.sunsetOffsetMin = 90;
  original.ntpServer = "time.example";
  original.gmtOffsetSec = -18000;
  original.daylightOffsetSec = 1800;
  original.lamps[0].name = "Backup lamp";
  original.buttonSources[0].enabled = false;
  original.motionSensors[0].pollingEnabled = false;
  original.espMotionEnabled = false;
  String error;
  size_t bytes = 99;
  uint32_t createdAt = 99;
  assert(getConfigBackupStatus(bytes, createdAt, error) && bytes == 0 && createdAt == 0);
  AppConfig restored;
  const std::string before = snapshot(restored);
  assert(!restoreConfigFromPreferences(restored, error));
  assert(snapshot(restored) == before && files.empty());
  std::puts("PASS: missing backup reports unavailable and restore does not change config or files");

  assert(backupConfigToPreferences(original, error));
  const std::string saved = nvsBlob;
  assert(getConfigBackupStatus(bytes, createdAt, error) && bytes == saved.size());
  files.clear();
  assert(restoreConfigFromPreferences(restored, error));
  assert(snapshot(restored) == snapshot(original));
  assert(*files.at("/config.json") == snapshot(original));
  assert(nvsBlob == saved);
  AppConfig reloaded;
  assert(loadConfig(reloaded) && snapshot(reloaded) == snapshot(original));
  std::puts("PASS: all config fields survive NVS roundtrip and simulated LittleFS replacement");

  original.lamps.clear(); original.motionSensors.clear(); original.buttonSources.clear();
  assert(backupConfigToPreferences(original, error));
  assert(restoreConfigFromPreferences(restored, error));
  assert(restored.lamps.empty() && restored.motionSensors.empty() && restored.buttonSources.empty());
  std::puts("PASS: explicit empty lists replace previous backup and remain empty on restore");

  nvsWriteFails = true;
  const std::string previous = nvsBlob;
  assert(!backupConfigToPreferences(AppConfig(), error));
  assert(nvsBlob == previous);
  nvsWriteFails = false;
  nvsOpenFails = true;
  assert(!getConfigBackupStatus(bytes, createdAt, error));
  assert(!backupConfigToPreferences(original, error));
  assert(!restoreConfigFromPreferences(restored, error));
  nvsOpenFails = false;
  nvsReadFails = true;
  assert(!restoreConfigFromPreferences(restored, error));
  assert(!backupConfigToPreferences(original, error));
  nvsReadFails = false;
  std::puts("PASS: full/write/open/read failures are explicit and a failed write preserves old backup");

  nvsBlob = saved;
  fileWriteFails = true;
  const std::string current = snapshot(restored);
  assert(!restoreConfigFromPreferences(restored, error));
  assert(snapshot(restored) == current);
  fileWriteFails = false;
  renameFails = true;
  const std::string oldFile = *files.at("/config.json");
  assert(!restoreConfigFromPreferences(restored, error));
  assert(snapshot(restored) == current && *files.at("/config.json") == oldFile);
  assert(!files.count("/config.tmp"));
  renameFails = false;
  std::puts("PASS: LittleFS failures preserve live config and the previous persisted file");

  for (const char *field : {"version", "config", "createdAt", "lamps", "buttonSources", "motionSensors",
                           "espMotionEnabled", "motionOffDelayMs", "espMotionPin", "latitude", "ntpServer"}) {
    JsonDocument envelope;
    assert(!deserializeJson(envelope, saved));
    if (std::string(field) == "version") envelope["version"] = 99;
    else if (std::string(field) == "config" || std::string(field) == "createdAt") envelope.remove(field);
    else envelope["config"].as<JsonObject>().remove(field);
    nvsBlob.clear(); serializeJson(envelope, nvsBlob);
    assert(!restoreConfigFromPreferences(restored, error));
    assert(!getConfigBackupStatus(bytes, createdAt, error));
    assert(snapshot(restored) == current);
  }
  for (const auto &invalid : std::vector<std::pair<const char *, double>>{
      {"motionOffDelayMs", 0}, {"espMotionPin", 256}, {"motionStartMin", 1440},
      {"latitude", 91}, {"longitude", -181}, {"sunriseOffsetMin", 181}}) {
    JsonDocument envelope;
    assert(!deserializeJson(envelope, saved));
    envelope["config"][invalid.first] = invalid.second;
    nvsBlob.clear(); serializeJson(envelope, nvsBlob);
    assert(!restoreConfigFromPreferences(restored, error));
    assert(snapshot(restored) == current);
  }
  std::puts("PASS: unsupported versions, missing fields, wrong types and out-of-range values are rejected");

  for (const std::string &invalid : {std::string("{"), std::string(""), std::string(65537, 'x'), std::string("{}\0garbage", 10)}) {
    nvsBlob = invalid;
    assert(!restoreConfigFromPreferences(restored, error));
    assert(snapshot(restored) == current);
  }
  std::puts("PASS: corrupt, empty, oversized and embedded-null backups cannot alter live config");

  original.motionOffDelayMs = 0;
  const unsigned int writesBefore = nvsWrites;
  assert(!backupConfigToPreferences(original, error));
  assert(nvsWrites == writesBefore);
  std::puts("PASS: invalid live snapshots are rejected before writing NVS");

  nvsBlob = saved;
  files.clear();
  AppConfig defaults;
  assert(loadConfig(defaults));
  assert(snapshot(defaults) == snapshot(AppConfig()));
  assert(nvsBlob == saved);
  std::puts("PASS: boot without LittleFS config uses defaults and never auto-restores NVS");
}
