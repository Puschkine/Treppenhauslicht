#pragma once

#include <Arduino.h>
#include <deque>
#include <mutex>
#include "app_config.h"

class LightingController {
public:
  explicit LightingController(AppConfig &config);

  void begin();
  void loop();
  void onConfigUpdated();

  bool onMotionEvent(const String &source, bool active);
  bool resolveMotionSource(const String &legacyNode, String &id, uint8_t &channel) const;
  void writeMotionStatus(JsonArray array) const;
  void handleShortPush();
  void handleLongPush();
  void forceOff();
  bool isShelly102CommandRecent(unsigned long guardMs) const;

  bool getRelayState() const;
  bool getMotionEsp() const;
  bool getMotion103() const;
  bool getMotion104() const;
  bool getForceOnLatch() const;
  bool getAutomationEnabled() const;
  bool shortOverrideActive() const;
  uint8_t activeMotionCount() const;
  bool isMotionWindowActive() const;
  bool getSunTimes(int &sunriseMin, int &sunsetMin) const;
  bool getEffectiveWindow(uint16_t &startMin, uint16_t &endMin) const;
  bool setSingleLamp(const String &id, bool on);
  void setAllLampsManual(bool on);
  void setAutomationEnabled(bool enabled);
  void writeLampStatus(JsonArray array) const;
  bool lampCommandsBusy() const;

private:
  AppConfig &cfg;

  struct MotionState {
    MotionSensorConfig config;
    bool active = false;
    bool hasEvent = false;
    bool timedOut = false;
    unsigned long lastEventMs = 0;
    String lastSource;
    bool pollKnown = false;
    bool pollHealthy = true;
    uint32_t sequence = 0;
  };
  mutable std::mutex motionMutex;
  MotionState localMotion;
  std::vector<MotionState> motionStates;
  uint32_t motionGeneration = 0;
  size_t nextMotionPoll = 0;
  bool motionPollCycle = false;
  uint8_t configuredLedPin = 255;
  bool relayState = false;
  bool forceOnLatch = false;

  unsigned long shortOverrideUntil = 0;
  unsigned long suppressMotionUntil = 0;
  unsigned long offDeadline = 0;
  unsigned long autoRearmBlockedUntil = 0;

  bool espRawState = false;
  bool espStableState = false;
  unsigned long espLastEdgeMs = 0;
  unsigned long lastShellyPollMs = 0;
  unsigned long lastShelly102CommandMs = 0;
  bool hasShelly102Command = false;

  mutable int sunCacheYday = -1;
  mutable int sunCacheSunriseMin = -1;
  mutable int sunCacheSunsetMin = -1;
  mutable unsigned long nextSunFetchMs = 0;

  static const unsigned long ESP_DEBOUNCE_MS = 120;
  static const unsigned long AUTO_RETRIGGER_GUARD_MS = 2000;

  bool isBeforeMillis(unsigned long now, unsigned long target) const;
  bool readEspMotion() const;
  bool getLegacyMotion(const String &node) const;
  void recordMotion(MotionState &state, bool active, const String &source);
  void updateEspMotionLed();
  void applyMotionPlausibilityTimeouts();
  void pollShellyInputs();
  bool queryShellyInputState(const String &ip, uint8_t channel, bool &state) const;
  bool resolveSunTimes(int &sunriseMin, int &sunsetMin) const;
  bool fetchSunTimesOnline(int &sunriseMin, int &sunsetMin) const;
  void evaluateLighting();
  bool computeDesiredOn() const;
  void setRelayState(bool on, const String &reason);
  struct LampState {
    LampConfig config;
    bool pending = false;
    bool requestedOn = false;
    bool commandKnown = false;
    bool lastCommandOn = false;
    int httpCode = 0;
    String error;
    uint32_t sequence = 0;
  };
  struct LampCommand {
    LampConfig config;
    bool on;
    uint32_t generation;
    uint32_t sequence;
    bool mandatory;
  };
  mutable std::mutex lampMutex;
  std::vector<LampState> lampStates;
  std::deque<LampCommand> lampCommands;
  uint32_t lampGeneration = 0;
  bool lampCommandInFlight = false;
  void resetLampCommands();
  void queueLampCommand(LampState &lamp, bool on, bool mandatory = false);
  void processLampCommand();
  void setConfiguredRelays(bool on);
  void setAllLampRelays(bool on, bool mandatory = false);
};