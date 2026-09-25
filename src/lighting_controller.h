#pragma once

#include <Arduino.h>
#include "app_config.h"

class LightingController {
public:
  explicit LightingController(AppConfig &config);

  void begin();
  void loop();
  void onConfigUpdated();

  void onMotionEvent(const String &source, bool active);
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
  bool setSingleLampRelay(uint16_t node, uint8_t relay, bool on);
  void setAllLampsManual(bool on);
  void setAutomationEnabled(bool enabled);

private:
  AppConfig &cfg;

  bool motionEsp = false;
  bool motion103 = false;
  bool motion104 = false;
  bool relayState = false;
  bool forceOnLatch = false;

  unsigned long shortOverrideUntil = 0;
  unsigned long suppressMotionUntil = 0;
  unsigned long offDeadline = 0;
  unsigned long autoRearmBlockedUntil = 0;

  bool espRawState = false;
  bool espStableState = false;
  unsigned long espLastEdgeMs = 0;
  unsigned long lastEventMsEsp = 0;
  unsigned long lastEventMs103 = 0;
  unsigned long lastEventMs104 = 0;
  unsigned long lastShellyPollMs = 0;
  unsigned long lastShelly102CommandMs = 0;
  bool pollHealthy103 = true;
  bool pollHealthy104 = true;

  static const unsigned long ESP_DEBOUNCE_MS = 120;
  static const unsigned long AUTO_RETRIGGER_GUARD_MS = 2000;

  bool isBeforeMillis(unsigned long now, unsigned long target) const;
  bool readEspMotion() const;
  void updateEspMotionLed();
  void applyMotionPlausibilityTimeouts();
  void pollShellyInputs();
  bool queryShellyInputState(const String &ip, uint8_t channel, bool &state) const;
  void evaluateLighting();
  bool computeDesiredOn() const;
  void setRelayState(bool on, const String &reason);
  void setConfiguredRelays(bool on) const;
  void setAllLampRelays(bool on) const;
  void sendShellyRelayCommand(const String &ip, uint8_t relay, bool on) const;
};