#include "lighting_controller.h"

#include <Det.hpp>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <time.h>

#include "debug_log.h"

LightingController::LightingController(AppConfig &config) : cfg(config)
{
}

void LightingController::begin()
{
  // Ensure a defined safe startup state: all lights off at boot.
  setAllLampRelays(false);
  relayState = false;
  forceOnLatch = false;
  shortOverrideUntil = 0;
  suppressMotionUntil = 0;
  offDeadline = 0;
  autoRearmBlockedUntil = 0;

  pinMode(cfg.espMotionPin, INPUT);
  pinMode(cfg.espMotionLedPin, OUTPUT);
  espRawState = readEspMotion();
  espStableState = espRawState;
  motionEsp = espStableState;
  espLastEdgeMs = millis();
  lastEventMsEsp = espLastEdgeMs;
  lastEventMs103 = espLastEdgeMs;
  lastEventMs104 = espLastEdgeMs;
  lastShellyPollMs = espLastEdgeMs;
  pollHealthy103 = true;
  pollHealthy104 = true;
  updateEspMotionLed();

  appLogf(APP_LOG_LEVEL, "Startup Action: Alle Lampen AUS");
}

void LightingController::loop()
{
  const unsigned long now = millis();

  bool nowRaw = readEspMotion();
  if (nowRaw != espRawState) {
    espRawState = nowRaw;
    espLastEdgeMs = now;
  }

  if (espStableState != espRawState && !isBeforeMillis(now, espLastEdgeMs + ESP_DEBOUNCE_MS)) {
    espStableState = espRawState;
    onMotionEvent("esp", espStableState);
  }

  pollShellyInputs();
  applyMotionPlausibilityTimeouts();
  evaluateLighting();
}

void LightingController::onConfigUpdated()
{
  begin();
  evaluateLighting();
}

void LightingController::onMotionEvent(const String &source, bool active)
{
  const char *sourceLabel = "unbekannt";
  if (source == "esp") {
    sourceLabel = "lokal";
  } else if (source == "103") {
    sourceLabel = "103";
  } else if (source == "104") {
    sourceLabel = "104";
  }

  if (source == "esp") {
    const unsigned long now = millis();
    lastEventMsEsp = now;
    motionEsp = active;
    updateEspMotionLed();
  } else if (source == "103") {
    motion103 = active;
    lastEventMs103 = millis();
  } else if (source == "104") {
    motion104 = active;
    lastEventMs104 = millis();
  }

  String msg = "BWM Action von " + String(sourceLabel) + ": " + String(active ? "EIN" : "AUS");
  appLogf(APP_LOG_LEVEL, "%s", msg.c_str());

  evaluateLighting();
}

void LightingController::handleShortPush()
{
  appLogf(APP_LOG_LEVEL, "Taster Action: SHORT");

  if (relayState) {
    forceOnLatch = false;
    shortOverrideUntil = 0;
    suppressMotionUntil = millis() + cfg.buttonShortOverrideMs;
    offDeadline = 0;
    setAllLampRelays(false);
    relayState = false;
    appLogf(APP_LOG_LEVEL, "Relais AUS wegen Short Push OFF (alle Kanaele)");
    return;
  }

  forceOnLatch = false;
  suppressMotionUntil = 0;
  shortOverrideUntil = millis() + cfg.buttonShortOverrideMs;
  offDeadline = 0;
  setRelayState(true, "Short Push ON");
}

void LightingController::handleLongPush()
{
  appLogf(APP_LOG_LEVEL, "Taster Action: LONG");

  forceOnLatch = true;
  shortOverrideUntil = 0;
  suppressMotionUntil = 0;
  offDeadline = 0;
  setAllLampRelays(true);
  relayState = true;
  appLogf(APP_LOG_LEVEL, "Relais AN wegen Long Push ON (alle Kanaele)");
}

void LightingController::forceOff()
{
  appLogf(APP_LOG_LEVEL, "Taster/API Action: FORCE OFF");

  forceOnLatch = false;
  shortOverrideUntil = 0;
  suppressMotionUntil = millis() + cfg.buttonShortOverrideMs;
  offDeadline = 0;
  setAllLampRelays(false);
  relayState = false;
  appLogf(APP_LOG_LEVEL, "Relais AUS wegen API OFF (alle Kanaele)");
}

bool LightingController::getRelayState() const
{
  return relayState;
}

bool LightingController::getMotionEsp() const
{
  return motionEsp;
}

bool LightingController::getMotion103() const
{
  return motion103;
}

bool LightingController::getMotion104() const
{
  return motion104;
}

bool LightingController::getForceOnLatch() const
{
  return forceOnLatch;
}

bool LightingController::isShelly102CommandRecent(unsigned long guardMs) const
{
  return isBeforeMillis(millis(), lastShelly102CommandMs + guardMs);
}

bool LightingController::shortOverrideActive() const
{
  return isBeforeMillis(millis(), shortOverrideUntil);
}

uint8_t LightingController::activeMotionCount() const
{
  uint8_t count = 0;
  if (motionEsp) {
    count++;
  }
  if (motion103) {
    count++;
  }
  if (motion104) {
    count++;
  }
  return count;
}

bool LightingController::isMotionWindowActive() const
{
  if (!cfg.timeWindowEnabled) {
    return true;
  }

  time_t now = time(nullptr);
  if (now < 100000) {
    return true;
  }

  struct tm localTime;
  localtime_r(&now, &localTime);
  uint16_t currentMin = (uint16_t)(localTime.tm_hour * 60 + localTime.tm_min);
  uint16_t start = cfg.motionStartMin % (24 * 60);
  uint16_t end = cfg.motionEndMin % (24 * 60);

  if (start == end) {
    return true;
  }
  if (start < end) {
    return currentMin >= start && currentMin < end;
  }
  return currentMin >= start || currentMin < end;
}

bool LightingController::isBeforeMillis(unsigned long now, unsigned long target) const
{
  return (long)(now - target) < 0;
}

bool LightingController::readEspMotion() const
{
  bool raw = digitalRead(cfg.espMotionPin);
  return cfg.espMotionInvert ? !raw : raw;
}

void LightingController::updateEspMotionLed()
{
  digitalWrite(cfg.espMotionLedPin, motionEsp ? HIGH : LOW);
}

void LightingController::applyMotionPlausibilityTimeouts()
{
  if (cfg.motionEventTimeoutMs == 0) {
    return;
  }

  const unsigned long now = millis();

  if (motionEsp && (unsigned long)(now - lastEventMsEsp) > cfg.motionEventTimeoutMs) {
    motionEsp = false;
    updateEspMotionLed();
    appLogf(APP_LOG_LEVEL, "Plausibilitaet: lokal BWM auf AUS (Timeout %lu ms)", (unsigned long)cfg.motionEventTimeoutMs);
  }

  if (motion103 && (unsigned long)(now - lastEventMs103) > cfg.motionEventTimeoutMs) {
    motion103 = false;
    appLogf(APP_LOG_LEVEL, "Plausibilitaet: BWM 103 auf AUS (Timeout %lu ms)", (unsigned long)cfg.motionEventTimeoutMs);
  }

  if (motion104 && (unsigned long)(now - lastEventMs104) > cfg.motionEventTimeoutMs) {
    motion104 = false;
    appLogf(APP_LOG_LEVEL, "Plausibilitaet: BWM 104 auf AUS (Timeout %lu ms)", (unsigned long)cfg.motionEventTimeoutMs);
  }
}

void LightingController::evaluateLighting()
{
  bool desiredOn = computeDesiredOn();
  bool anyMotionActive = activeMotionCount() > 0;

  if (desiredOn) {
    offDeadline = 0;
    setRelayState(true, "aktive Steuerung");
    return;
  }

  // Nachlauf darf erst anlaufen, wenn kein BWM mehr aktiv ist.
  if (anyMotionActive) {
    offDeadline = 0;
    return;
  }

  if (!relayState) {
    offDeadline = 0;
    return;
  }

  unsigned long now = millis();
  if (offDeadline == 0) {
    offDeadline = now + cfg.motionOffDelayMs;
    appLogf(APP_LOG_LEVEL, "Nachlauf gestartet: %lu ms", (unsigned long)cfg.motionOffDelayMs);
    return;
  }

  if (!isBeforeMillis(now, offDeadline)) {
    // Re-check before OFF to avoid immediate re-on when a fresh motion edge
    // arrives right around the timeout boundary.
    if (activeMotionCount() > 0 || computeDesiredOn()) {
      offDeadline = 0;
      return;
    }
    setRelayState(false, "Nachlauf Ende");
    autoRearmBlockedUntil = millis() + AUTO_RETRIGGER_GUARD_MS;
    appLogf(APP_LOG_LEVEL, "Re-Trigger Sperre aktiv: %lu ms", (unsigned long)AUTO_RETRIGGER_GUARD_MS);
    offDeadline = 0;
  }
}

bool LightingController::computeDesiredOn() const
{
  unsigned long now = millis();
  if (forceOnLatch) {
    return true;
  }
  if (isBeforeMillis(now, shortOverrideUntil)) {
    return true;
  }
  if (isBeforeMillis(now, suppressMotionUntil)) {
    return false;
  }
  if (activeMotionCount() > 0 && isBeforeMillis(now, autoRearmBlockedUntil)) {
    return false;
  }
  if (!isMotionWindowActive()) {
    return false;
  }
  return activeMotionCount() > 0;
}

void LightingController::setRelayState(bool on, const String &reason)
{
  if (relayState == on) {
    return;
  }
  relayState = on;
  setConfiguredRelays(on);
  String msg = "Relais " + String(on ? "AN" : "AUS") + " wegen " + reason;
  appLogf(APP_LOG_LEVEL, "%s", msg.c_str());
}

void LightingController::setConfiguredRelays(bool on) const
{
  sendShellyRelayCommand(cfg.shelly102Ip, 0, on);
  sendShellyRelayCommand(cfg.shelly103Ip, cfg.shelly103OutputChannel, on);
  sendShellyRelayCommand(cfg.shelly104Ip, cfg.shelly104OutputChannel, on);
}

void LightingController::setAllLampRelays(bool on) const
{
  sendShellyRelayCommand(cfg.shelly102Ip, 0, on);
  sendShellyRelayCommand(cfg.shelly103Ip, 0, on);
  sendShellyRelayCommand(cfg.shelly103Ip, 1, on);
  sendShellyRelayCommand(cfg.shelly104Ip, 0, on);
  sendShellyRelayCommand(cfg.shelly104Ip, 1, on);
}

void LightingController::sendShellyRelayCommand(const String &ip, uint8_t relay, bool on) const
{
  if (ip.length() == 0) {
    return;
  }

  if (ip == cfg.shelly102Ip) {
    const_cast<LightingController *>(this)->lastShelly102CommandMs = millis();
  }

  String url = "http://" + ip + "/rpc/Switch.Set?id=" + String(relay) + "&on=" + (on ? "true" : "false");
  HTTPClient http;
  http.setTimeout(700);
  http.begin(url);
  int code = http.GET();
  if (code < 200 || code >= 300) {
    String msg = "Shelly RPC Fehler bei " + url + " (HTTP " + String(code) + ")";
    appLogf(APP_LOG_LEVEL, "%s", msg.c_str());
  }
  http.end();
}

void LightingController::pollShellyInputs()
{
  if (!cfg.shellyPollingEnabled || cfg.shellyPollIntervalMs == 0) {
    return;
  }

  const unsigned long now = millis();
  if (isBeforeMillis(now, lastShellyPollMs + cfg.shellyPollIntervalMs)) {
    return;
  }
  lastShellyPollMs = now;

  bool polledState = false;
  bool ok103 = queryShellyInputState(cfg.shelly103Ip, cfg.shelly103MotionChannel, polledState);
  if (ok103) {
    if (!pollHealthy103) {
      appLogf(APP_LOG_LEVEL, "Polling 103 wieder ok");
    }
    pollHealthy103 = true;

    if (polledState != motion103) {
      appLogf(APP_LOG_LEVEL, "Polling 103 korrigiert Zustand auf %s", polledState ? "EIN" : "AUS");
      onMotionEvent("103", polledState);
    } else if (polledState) {
      lastEventMs103 = now;
    }
  } else if (pollHealthy103) {
    pollHealthy103 = false;
    appLogf(APP_LOG_LEVEL, "Polling 103 fehlgeschlagen");
  }

  bool ok104 = queryShellyInputState(cfg.shelly104Ip, cfg.shelly104MotionChannel, polledState);
  if (ok104) {
    if (!pollHealthy104) {
      appLogf(APP_LOG_LEVEL, "Polling 104 wieder ok");
    }
    pollHealthy104 = true;

    if (polledState != motion104) {
      appLogf(APP_LOG_LEVEL, "Polling 104 korrigiert Zustand auf %s", polledState ? "EIN" : "AUS");
      onMotionEvent("104", polledState);
    } else if (polledState) {
      lastEventMs104 = now;
    }
  } else if (pollHealthy104) {
    pollHealthy104 = false;
    appLogf(APP_LOG_LEVEL, "Polling 104 fehlgeschlagen");
  }
}

bool LightingController::queryShellyInputState(const String &ip, uint8_t channel, bool &state) const
{
  if (ip.length() == 0) {
    return false;
  }

  String url = "http://" + ip + "/rpc/Input.GetStatus?id=" + String(channel);
  HTTPClient http;
  http.setTimeout(1500);
  http.begin(url);
  int code = http.GET();
  if (code < 200 || code >= 300) {
    http.end();
    appLogf(APP_LOG_LEVEL, "Polling RPC Fehler bei Shelly %s: HTTP %d", ip.c_str(), code);
    return false;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    appLogf(APP_LOG_LEVEL, "Polling RPC JSON Fehler bei Shelly %s", ip.c_str());
    return false;
  }

  JsonVariant st = doc["state"];
  if (st.is<bool>()) {
    state = st.as<bool>();
    return true;
  }
  if (st.is<int>()) {
    state = st.as<int>() != 0;
    return true;
  }

  appLogf(APP_LOG_LEVEL, "Polling RPC ohne state bei Shelly %s", ip.c_str());
  return false;
}