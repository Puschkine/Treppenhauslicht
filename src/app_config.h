#pragma once

#include <Arduino.h>

struct AppConfig {
  String shelly102Ip = "192.168.178.102";
  String shelly103Ip = "192.168.178.103";
  String shelly104Ip = "192.168.178.104";
  uint8_t shelly103MotionChannel = 1;
  uint8_t shelly104MotionChannel = 0;
  uint8_t shelly103OutputChannel = 0;
  uint8_t shelly104OutputChannel = 0;
  uint32_t motionOffDelayMs = 60000;
  uint32_t buttonShortOverrideMs = 120000;
  uint32_t motionEventTimeoutMs = 180000;
  bool shellyPollingEnabled = true;
  uint32_t shellyPollIntervalMs = 5000;
  uint8_t espMotionPin = 12;
  uint8_t espMotionLedPin = 32;
  bool espMotionInvert = false;
  bool timeWindowEnabled = false;
  uint16_t motionStartMin = 0;
  uint16_t motionEndMin = 0;
  String ntpServer = "pool.ntp.org";
  long gmtOffsetSec = 3600;
  int daylightOffsetSec = 3600;
};

void applyTimeConfig(const AppConfig &cfg);
bool saveConfig(const AppConfig &cfg);
bool loadConfig(AppConfig &cfg);
String hhmmFromMinutes(uint16_t mins);