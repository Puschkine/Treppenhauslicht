#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

constexpr size_t MAX_LAMPS = 32;
constexpr size_t MAX_MOTION_SENSORS = 32;
constexpr size_t MAX_BUTTON_SOURCES = 32;

struct ButtonSourceConfig {
  String name;
  String ip;
  bool enabled = true;
  bool buttonGuard = false;
};

struct MotionSensorConfig {
  String id;
  String name;
  bool enabled = true;
  bool pollingEnabled = false;
  String pollHost;
  uint8_t pollChannel = 0;
  String legacyNode;
};

struct LampConfig {
  String id;
  String name;
  String onUrl;
  String offUrl;
  bool enabled = true;
  bool automation = true;
  bool buttonGuard = false;
};

struct AppConfig {
  AppConfig();
  std::vector<ButtonSourceConfig> buttonSources;
  std::vector<LampConfig> lamps;
  std::vector<MotionSensorConfig> motionSensors;
  bool espMotionEnabled = true;
  uint32_t motionOffDelayMs = 60000;
  uint32_t buttonShortOverrideMs = 120000;
  uint32_t motionEventTimeoutMs = 180000;
  uint32_t shellyPollIntervalMs = 5000;
  uint8_t espMotionPin = 12;
  uint8_t espMotionLedPin = 32;
  bool espMotionInvert = false;
  bool timeWindowEnabled = false;
  bool automationEnabled = true;
  uint16_t motionStartMin = 0;
  uint16_t motionEndMin = 0;
  bool windowUseSun = false;
  bool sunOnlineLookup = true;
  double latitude = 52.5200;
  double longitude = 13.4050;
  int16_t sunriseOffsetMin = 0;
  int16_t sunsetOffsetMin = 0;
  String ntpServer = "pool.ntp.org";
  long gmtOffsetSec = 3600;
  int daylightOffsetSec = 3600;
};

void writeLampConfig(JsonArray array, const std::vector<LampConfig> &lamps);
bool readLampConfig(JsonVariantConst value, std::vector<LampConfig> &lamps, String &error);
void writeMotionConfig(JsonArray array, const std::vector<MotionSensorConfig> &sensors);
bool readMotionConfig(JsonVariantConst value, std::vector<MotionSensorConfig> &sensors, String &error);
void writeButtonConfig(JsonArray array, const std::vector<ButtonSourceConfig> &sources);
bool readButtonConfig(JsonVariantConst value, std::vector<ButtonSourceConfig> &sources, String &error);
void applyTimeConfig(const AppConfig &cfg);
bool saveConfig(const AppConfig &cfg);
bool loadConfig(AppConfig &cfg);
bool getConfigBackupStatus(size_t &bytes, uint32_t &createdAt, String &error);
bool backupConfigToPreferences(const AppConfig &cfg, String &error);
bool restoreConfigFromPreferences(AppConfig &restored, String &error);
String hhmmFromMinutes(uint16_t mins);