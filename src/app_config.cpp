#include "app_config.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <time.h>

#include "debug_log.h"

void applyTimeConfig(const AppConfig &cfg)
{
  configTime(cfg.gmtOffsetSec, cfg.daylightOffsetSec, cfg.ntpServer.c_str());
}

bool saveConfig(const AppConfig &cfg)
{
  JsonDocument doc;
  doc["shelly102Ip"] = cfg.shelly102Ip;
  doc["shelly103Ip"] = cfg.shelly103Ip;
  doc["shelly104Ip"] = cfg.shelly104Ip;
  doc["shelly103MotionChannel"] = cfg.shelly103MotionChannel;
  doc["shelly104MotionChannel"] = cfg.shelly104MotionChannel;
  doc["shelly103OutputChannel"] = cfg.shelly103OutputChannel;
  doc["shelly104OutputChannel"] = cfg.shelly104OutputChannel;
  doc["motionOffDelayMs"] = cfg.motionOffDelayMs;
  doc["buttonShortOverrideMs"] = cfg.buttonShortOverrideMs;
  doc["motionEventTimeoutMs"] = cfg.motionEventTimeoutMs;
  doc["shellyPollingEnabled"] = cfg.shellyPollingEnabled;
  doc["shellyPollIntervalMs"] = cfg.shellyPollIntervalMs;
  doc["espMotionPin"] = cfg.espMotionPin;
  doc["espMotionLedPin"] = cfg.espMotionLedPin;
  doc["espMotionInvert"] = cfg.espMotionInvert;
  doc["timeWindowEnabled"] = cfg.timeWindowEnabled;
  doc["automationEnabled"] = cfg.automationEnabled;
  doc["motionStartMin"] = cfg.motionStartMin;
  doc["motionEndMin"] = cfg.motionEndMin;
  doc["ntpServer"] = cfg.ntpServer;
  doc["gmtOffsetSec"] = cfg.gmtOffsetSec;
  doc["daylightOffsetSec"] = cfg.daylightOffsetSec;

  File f = LittleFS.open("/config.json", "w");
  if (!f) {
    appLogf(APP_LOG_LEVEL, "Konnte config.json nicht schreiben");
    return false;
  }
  serializeJson(doc, f);
  f.close();
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

  cfg.shelly102Ip = doc["shelly102Ip"] | cfg.shelly102Ip;
  cfg.shelly103Ip = doc["shelly103Ip"] | cfg.shelly103Ip;
  cfg.shelly104Ip = doc["shelly104Ip"] | cfg.shelly104Ip;
  cfg.shelly103MotionChannel = doc["shelly103MotionChannel"] | cfg.shelly103MotionChannel;
  cfg.shelly104MotionChannel = doc["shelly104MotionChannel"] | cfg.shelly104MotionChannel;
  cfg.shelly103OutputChannel = doc["shelly103OutputChannel"] | cfg.shelly103OutputChannel;
  cfg.shelly104OutputChannel = doc["shelly104OutputChannel"] | cfg.shelly104OutputChannel;
  cfg.motionOffDelayMs = doc["motionOffDelayMs"] | cfg.motionOffDelayMs;
  cfg.buttonShortOverrideMs = doc["buttonShortOverrideMs"] | cfg.buttonShortOverrideMs;
  cfg.motionEventTimeoutMs = doc["motionEventTimeoutMs"] | cfg.motionEventTimeoutMs;
  cfg.shellyPollingEnabled = doc["shellyPollingEnabled"] | cfg.shellyPollingEnabled;
  cfg.shellyPollIntervalMs = doc["shellyPollIntervalMs"] | cfg.shellyPollIntervalMs;
  cfg.espMotionPin = doc["espMotionPin"] | cfg.espMotionPin;
  cfg.espMotionLedPin = doc["espMotionLedPin"] | cfg.espMotionLedPin;
  cfg.espMotionInvert = doc["espMotionInvert"] | cfg.espMotionInvert;
  cfg.timeWindowEnabled = doc["timeWindowEnabled"] | cfg.timeWindowEnabled;
  cfg.automationEnabled = doc["automationEnabled"] | cfg.automationEnabled;
  cfg.motionStartMin = doc["motionStartMin"] | cfg.motionStartMin;
  cfg.motionEndMin = doc["motionEndMin"] | cfg.motionEndMin;
  cfg.ntpServer = doc["ntpServer"] | cfg.ntpServer;
  cfg.gmtOffsetSec = doc["gmtOffsetSec"] | cfg.gmtOffsetSec;
  cfg.daylightOffsetSec = doc["daylightOffsetSec"] | cfg.daylightOffsetSec;
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