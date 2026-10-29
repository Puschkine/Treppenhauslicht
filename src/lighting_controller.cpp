#include "lighting_controller.h"

#include <Det.hpp>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <math.h>
#include <time.h>

#include "debug_log.h"

namespace {

// NOAA/Almanac sunrise equation, accurate to a few minutes; good enough for lighting control.
bool computeSunEvent(int dayOfYear, double latDeg, double lonDeg, double utcOffsetHours, bool sunrise, int &resultMin)
{
  const double DEG2RAD = M_PI / 180.0;
  const double RAD2DEG = 180.0 / M_PI;

  double lngHour = lonDeg / 15.0;
  double t = sunrise ? (dayOfYear + ((6.0 - lngHour) / 24.0)) : (dayOfYear + ((18.0 - lngHour) / 24.0));

  double M = (0.9856 * t) - 3.289;

  double L = M + (1.916 * sin(M * DEG2RAD)) + (0.020 * sin(2 * M * DEG2RAD)) + 282.634;
  L = fmod(L + 360.0, 360.0);

  double RA = RAD2DEG * atan(0.91764 * tan(L * DEG2RAD));
  RA = fmod(RA + 360.0, 360.0);
  RA += (floor(L / 90.0) * 90.0) - (floor(RA / 90.0) * 90.0);
  RA /= 15.0;

  double sinDec = 0.39782 * sin(L * DEG2RAD);
  double cosDec = cos(asin(sinDec));

  double cosH = (cos(90.833 * DEG2RAD) - (sinDec * sin(latDeg * DEG2RAD))) / (cosDec * cos(latDeg * DEG2RAD));
  if (cosH > 1.0 || cosH < -1.0) {
    return false; // Sonne geht an diesem Tag/Ort nicht auf bzw. unter
  }

  double H = sunrise ? (360.0 - RAD2DEG * acos(cosH)) : (RAD2DEG * acos(cosH));
  H /= 15.0;

  double T = H + RA - (0.06571 * t) - 6.622;

  double UT = fmod(T - lngHour + 24.0, 24.0);
  double localT = fmod(UT + utcOffsetHours + 24.0, 24.0);

  resultMin = (int)round(localT * 60.0) % (24 * 60);
  return true;
}

} // namespace

LightingController::LightingController(AppConfig &config) : cfg(config)
{
}

void LightingController::begin()
{
  resetLampCommands();
  // Ensure a defined safe startup state: all lights off at boot.
  setAllLampRelays(false, true);
  relayState = false;
  forceOnLatch = false;
  shortOverrideUntil = 0;
  suppressMotionUntil = 0;
  offDeadline = 0;
  autoRearmBlockedUntil = 0;

  {
    std::lock_guard<std::mutex> lock(motionMutex);
    ++motionGeneration;
    std::vector<MotionState> updatedStates;
    for (const MotionSensorConfig &config : cfg.motionSensors) {
      MotionState state;
      for (const MotionState &existing : motionStates) {
        if (existing.config.id == config.id && existing.config.enabled && config.enabled) {
          state = existing;
          if (existing.config.pollHost != config.pollHost ||
              existing.config.pollChannel != config.pollChannel ||
              existing.config.pollingEnabled != config.pollingEnabled) {
            state.pollKnown = false;
            state.pollHealthy = true;
          }
          break;
        }
      }
      state.config = config;
      updatedStates.push_back(state);
    }
    motionStates = std::move(updatedStates);
    if (configuredLedPin != 255) digitalWrite(configuredLedPin, LOW);
    configuredLedPin = cfg.espMotionLedPin;
    pinMode(configuredLedPin, OUTPUT);
    localMotion = MotionState();
    localMotion.config.id = "esp";
    localMotion.config.name = "Interner BWM";
    localMotion.config.enabled = cfg.espMotionEnabled;
    if (cfg.espMotionEnabled) pinMode(cfg.espMotionPin, INPUT);
    espRawState = cfg.espMotionEnabled && readEspMotion();
    espStableState = espRawState;
    if (cfg.espMotionEnabled) recordMotion(localMotion, espStableState, "gpio");
    espLastEdgeMs = millis();
    lastShellyPollMs = espLastEdgeMs;
    motionPollCycle = false;
    nextMotionPoll = 0;
    updateEspMotionLed();
  }

  appLogf(APP_LOG_LEVEL, "Startup Action: Alle Lampen AUS");
}

void LightingController::loop()
{
  const unsigned long now = millis();

  {
    std::lock_guard<std::mutex> lock(motionMutex);
    if (localMotion.config.enabled) {
      const bool nowRaw = readEspMotion();
      if (nowRaw != espRawState) {
        espRawState = nowRaw;
        espLastEdgeMs = now;
      }
      if (espStableState != espRawState && !isBeforeMillis(now, espLastEdgeMs + ESP_DEBOUNCE_MS)) {
        espStableState = espRawState;
        recordMotion(localMotion, espStableState, "gpio");
        updateEspMotionLed();
      }
    }
  }

  pollShellyInputs();
  applyMotionPlausibilityTimeouts();
  evaluateLighting();
  processLampCommand();
}

void LightingController::onConfigUpdated()
{
  sunCacheYday = -1;
  nextSunFetchMs = 0;
  begin();
  evaluateLighting();
}

void LightingController::recordMotion(MotionState &state, bool active, const String &source)
{
  const bool changed = state.active != active;
  state.active = active;
  state.hasEvent = true;
  state.timedOut = false;
  state.lastEventMs = millis();
  state.lastSource = source;
  ++state.sequence;
  if (changed || source != "poll") {
    appLogf(APP_LOG_LEVEL, "BWM %s (%s): %s via %s", state.config.name.c_str(),
            state.config.id.c_str(), active ? "EIN" : "AUS", source.c_str());
  }
}

bool LightingController::onMotionEvent(const String &source, bool active)
{
  bool accepted = false;
  {
    std::lock_guard<std::mutex> lock(motionMutex);
    if (source == "esp" && localMotion.config.enabled) {
      recordMotion(localMotion, active, "http");
      updateEspMotionLed();
      accepted = true;
    } else {
      for (MotionState &state : motionStates) {
        if (state.config.id == source && state.config.enabled) {
          recordMotion(state, active, "http");
          accepted = true;
          break;
        }
      }
    }
  }
  if (!accepted) {
    appLogf(APP_LOG_LEVEL, "BWM Event verworfen: unbekannt oder deaktiviert: %s", source.c_str());
    return false;
  }
  evaluateLighting();
  return true;
}

bool LightingController::resolveMotionSource(const String &node, String &id, uint8_t &channel) const
{
  std::lock_guard<std::mutex> lock(motionMutex);
  if (node == "esp") {
    id = "esp";
    channel = 0;
    return true;
  }
  for (const MotionState &state : motionStates) {
    if (state.config.legacyNode == node && !node.isEmpty()) {
      id = state.config.id;
      channel = state.config.pollChannel;
      return true;
    }
  }
  return false;
}

void LightingController::handleShortPush()
{
  appLogf(APP_LOG_LEVEL, "Taster Action: SHORT");

  if (relayState) {
    const bool hadForceOnLatch = forceOnLatch;
    forceOnLatch = false;
    shortOverrideUntil = 0;
    suppressMotionUntil = hadForceOnLatch ? 0 : millis() + cfg.buttonShortOverrideMs;
    offDeadline = 0;
    setAllLampRelays(false);
    relayState = false;
    if (hadForceOnLatch) {
      appLogf(APP_LOG_LEVEL, "Relais AUS wegen Short Push nach Long-Latch (alle Kanaele)");
    } else {
      appLogf(APP_LOG_LEVEL, "Relais AUS wegen Short Push OFF (alle Kanaele)");
    }
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
  std::lock_guard<std::mutex> lock(motionMutex);
  return localMotion.config.enabled && localMotion.active;
}

bool LightingController::getMotion103() const
{
  return getLegacyMotion("103");
}

bool LightingController::getMotion104() const
{
  return getLegacyMotion("104");
}

bool LightingController::getLegacyMotion(const String &node) const
{
  std::lock_guard<std::mutex> lock(motionMutex);
  for (const MotionState &state : motionStates) {
    if (state.config.legacyNode == node) return state.config.enabled && state.active;
  }
  return false;
}

bool LightingController::getForceOnLatch() const
{
  return forceOnLatch;
}

bool LightingController::getAutomationEnabled() const
{
  return cfg.automationEnabled;
}

bool LightingController::isShelly102CommandRecent(unsigned long guardMs) const
{
  std::lock_guard<std::mutex> lock(lampMutex);
  return hasShelly102Command && isBeforeMillis(millis(), lastShelly102CommandMs + guardMs);
}

bool LightingController::shortOverrideActive() const
{
  return isBeforeMillis(millis(), shortOverrideUntil);
}

uint8_t LightingController::activeMotionCount() const
{
  std::lock_guard<std::mutex> lock(motionMutex);
  uint8_t count = localMotion.config.enabled && localMotion.active ? 1 : 0;
  for (const MotionState &state : motionStates) {
    if (state.config.enabled && state.active) ++count;
  }
  return count;
}

void LightingController::writeMotionStatus(JsonArray array) const
{
  std::lock_guard<std::mutex> lock(motionMutex);
  const unsigned long now = millis();
  const auto append = [&](const MotionState &state, bool local) {
    JsonObject item = array.add<JsonObject>();
    item["id"] = state.config.id;
    item["name"] = state.config.name;
    item["enabled"] = state.config.enabled;
    item["local"] = local;
    item["active"] = state.config.enabled && state.active;
    item["timedOut"] = state.timedOut;
    item["lastSource"] = state.lastSource;
    if (state.hasEvent) item["lastEventAgeMs"] = (uint32_t)(now - state.lastEventMs);
    item["pollingEnabled"] = state.config.pollingEnabled;
    if (state.pollKnown) item["pollHealthy"] = state.pollHealthy;
  };
  append(localMotion, true);
  for (const MotionState &state : motionStates) append(state, false);
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
  uint16_t start;
  uint16_t end;
  if (!getEffectiveWindow(start, end)) {
    return true; // Keine Sonnenauf-/untergang ermittelbar (z.B. Polarregion) -> immer aktiv
  }

  if (start == end) {
    return true;
  }
  if (start < end) {
    return currentMin >= start && currentMin < end;
  }
  return currentMin >= start || currentMin < end;
}

bool LightingController::getSunTimes(int &sunriseMin, int &sunsetMin) const
{
  return resolveSunTimes(sunriseMin, sunsetMin);
}

bool LightingController::getEffectiveWindow(uint16_t &startMin, uint16_t &endMin) const
{
  if (cfg.windowUseSun) {
    int sunriseMin = 0;
    int sunsetMin = 0;
    if (!resolveSunTimes(sunriseMin, sunsetMin)) {
      return false;
    }
    startMin = (uint16_t)(((sunsetMin - cfg.sunsetOffsetMin) % (24 * 60) + (24 * 60)) % (24 * 60));
    endMin = (uint16_t)(((sunriseMin + cfg.sunriseOffsetMin) % (24 * 60) + (24 * 60)) % (24 * 60));
  } else {
    startMin = cfg.motionStartMin % (24 * 60);
    endMin = cfg.motionEndMin % (24 * 60);
  }
  return true;
}

bool LightingController::resolveSunTimes(int &sunriseMin, int &sunsetMin) const
{
  time_t now = time(nullptr);
  if (now < 100000) {
    return false;
  }

  struct tm localTime;
  localtime_r(&now, &localTime);

  if (sunCacheYday == localTime.tm_yday) {
    sunriseMin = sunCacheSunriseMin;
    sunsetMin = sunCacheSunsetMin;
    return true;
  }

  if (cfg.sunOnlineLookup && WiFi.status() == WL_CONNECTED && !isBeforeMillis(millis(), nextSunFetchMs)) {
    if (fetchSunTimesOnline(sunriseMin, sunsetMin)) {
      sunCacheYday = localTime.tm_yday;
      sunCacheSunriseMin = sunriseMin;
      sunCacheSunsetMin = sunsetMin;
      nextSunFetchMs = millis() + 6UL * 60UL * 60UL * 1000UL; // heute erledigt, naechster Versuch in 6h
      return true;
    }
    nextSunFetchMs = millis() + 5UL * 60UL * 1000UL; // Fehler: in 5 Minuten erneut versuchen
  }

  const long daylightOffset = localTime.tm_isdst > 0 ? cfg.daylightOffsetSec : 0;
  double utcOffsetHours = (cfg.gmtOffsetSec + daylightOffset) / 3600.0;
  bool haveSunrise = computeSunEvent(localTime.tm_yday + 1, cfg.latitude, cfg.longitude, utcOffsetHours, true, sunriseMin);
  bool haveSunset = computeSunEvent(localTime.tm_yday + 1, cfg.latitude, cfg.longitude, utcOffsetHours, false, sunsetMin);
  return haveSunrise && haveSunset;
}

bool LightingController::fetchSunTimesOnline(int &sunriseMin, int &sunsetMin) const
{
  String url = "http://api.sunrise-sunset.org/v2?lat=" + String(cfg.latitude, 6) + "&lng=" + String(cfg.longitude, 6) +
               "&date=today&time_format=unix";
  HTTPClient http;
  http.setTimeout(4000);
  http.begin(url);
  int code = http.GET();
  if (code < 200 || code >= 300) {
    http.end();
    appLogf(APP_LOG_LEVEL, "Sonnenzeiten-API Fehler: HTTP %d", code);
    return false;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    appLogf(APP_LOG_LEVEL, "Sonnenzeiten-API JSON Fehler");
    return false;
  }

  if (!doc["sunrise"].is<long>() || !doc["sunset"].is<long>()) {
    appLogf(APP_LOG_LEVEL, "Sonnenzeiten-API ohne gueltige Zeiten (Status: %s)", doc["sun_status"] | "unbekannt");
    return false;
  }

  time_t sunriseEpoch = (time_t)doc["sunrise"].as<long>();
  time_t sunsetEpoch = (time_t)doc["sunset"].as<long>();

  struct tm tmSunrise;
  struct tm tmSunset;
  localtime_r(&sunriseEpoch, &tmSunrise);
  localtime_r(&sunsetEpoch, &tmSunset);
  sunriseMin = tmSunrise.tm_hour * 60 + tmSunrise.tm_min;
  sunsetMin = tmSunset.tm_hour * 60 + tmSunset.tm_min;

  appLogf(APP_LOG_LEVEL, "Sonnenzeiten online geladen: Aufgang %02d:%02d, Untergang %02d:%02d", tmSunrise.tm_hour, tmSunrise.tm_min,
          tmSunset.tm_hour, tmSunset.tm_min);
  return true;
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
  digitalWrite(configuredLedPin, localMotion.config.enabled && localMotion.active ? HIGH : LOW);
}

void LightingController::applyMotionPlausibilityTimeouts()
{
  if (cfg.motionEventTimeoutMs == 0) {
    return;
  }

  const unsigned long now = millis();

  std::lock_guard<std::mutex> lock(motionMutex);
  const auto expire = [&](MotionState &state) {
    if (state.config.enabled && state.active && (unsigned long)(now - state.lastEventMs) > cfg.motionEventTimeoutMs) {
      state.active = false;
      state.timedOut = true;
      ++state.sequence;
      appLogf(APP_LOG_LEVEL, "Plausibilitaet: BWM %s auf AUS (Timeout %lu ms)",
              state.config.name.c_str(), (unsigned long)cfg.motionEventTimeoutMs);
    }
  };
  expire(localMotion);
  for (MotionState &state : motionStates) expire(state);
  updateEspMotionLed();
}

void LightingController::evaluateLighting()
{
  if (!cfg.automationEnabled && !forceOnLatch && !isBeforeMillis(millis(), shortOverrideUntil)) {
    offDeadline = 0;
    return;
  }

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
  if (!cfg.automationEnabled) {
    return false;
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

void LightingController::setConfiguredRelays(bool on)
{
  std::lock_guard<std::mutex> lock(lampMutex);
  for (LampState &lamp : lampStates) {
    if (lamp.config.enabled && lamp.config.automation) queueLampCommand(lamp, on);
  }
}

void LightingController::setAllLampRelays(bool on, bool mandatory)
{
  std::lock_guard<std::mutex> lock(lampMutex);
  for (LampState &lamp : lampStates) {
    if (lamp.config.enabled) queueLampCommand(lamp, on, mandatory);
  }
}

void LightingController::resetLampCommands()
{
  std::lock_guard<std::mutex> lock(lampMutex);
  // Keep the old OFF target when a lamp is removed, disabled or reassigned.
  std::deque<LampCommand> cleanup;
  for (const LampCommand &command : lampCommands) {
    if (command.config.id.isEmpty()) cleanup.push_back(command);
  }
  for (const LampState &old : lampStates) {
    if (!old.config.enabled) continue;
    bool retained = false;
    for (const LampConfig &lamp : cfg.lamps) {
      if (lamp.enabled && lamp.id == old.config.id &&
          lamp.onUrl == old.config.onUrl && lamp.offUrl == old.config.offUrl) retained = true;
    }
    if (!retained) {
      LampConfig target = old.config;
      target.id = "";
      cleanup.push_back({target, false, 0, 0, true});
    }
  }
  lampCommands = std::move(cleanup);
  ++lampGeneration;
  lampStates.clear();
  for (const LampConfig &config : cfg.lamps) {
    LampState state;
    state.config = config;
    lampStates.push_back(state);
  }
}

void LightingController::queueLampCommand(LampState &lamp, bool on, bool mandatory)
{
  lamp.pending = true;
  lamp.requestedOn = on;
  ++lamp.sequence;
  for (LampCommand &command : lampCommands) {
    if (!command.mandatory && command.config.id == lamp.config.id && command.generation == lampGeneration) {
      command.on = on;
      command.sequence = lamp.sequence;
      return;
    }
  }
  LampCommand command{lamp.config, on, lampGeneration, lamp.sequence, mandatory};
  lampCommands.push_back(command);
}

void LightingController::processLampCommand()
{
  LampCommand command;
  {
    std::lock_guard<std::mutex> lock(lampMutex);
    if (lampCommands.empty()) return;
    command = lampCommands.front();
    lampCommands.pop_front();
    lampCommandInFlight = true;
    if (command.config.buttonGuard) {
      hasShelly102Command = true;
      lastShelly102CommandMs = millis();
    }
  }
  const String &url = command.on ? command.config.onUrl : command.config.offUrl;
  HTTPClient http;
  http.setConnectTimeout(700);
  http.setTimeout(700);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  const int code = http.begin(url) ? http.GET() : HTTPC_ERROR_CONNECTION_REFUSED;
  const bool ok = code >= 200 && code < 300;
  String error;
  if (!ok) {
    error = code > 0 ? "HTTP " + String(code) : HTTPClient::errorToString(code);
  }
  http.end();
  {
    std::lock_guard<std::mutex> lock(lampMutex);
    lampCommandInFlight = false;
    if (command.config.buttonGuard) lastShelly102CommandMs = millis();
    if (command.generation == lampGeneration) {
      for (LampState &lamp : lampStates) {
        if (lamp.config.id != command.config.id) continue;
        lamp.commandKnown = true;
        lamp.lastCommandOn = command.on;
        lamp.httpCode = code;
        lamp.error = error;
        if (lamp.sequence == command.sequence) lamp.pending = false;
      }
    }
  }
  appLogf(APP_LOG_LEVEL, "Lampe %s: %s %s (HTTP %d)",
          command.config.name.c_str(), command.on ? "EIN" : "AUS",
          ok ? "gesendet" : error.c_str(), code);
}

bool LightingController::lampCommandsBusy() const
{
  std::lock_guard<std::mutex> lock(lampMutex);
  return lampCommandInFlight || !lampCommands.empty();
}

void LightingController::writeLampStatus(JsonArray array) const
{
  std::lock_guard<std::mutex> lock(lampMutex);
  for (const LampState &lamp : lampStates) {
    JsonObject item = array.add<JsonObject>();
    item["id"] = lamp.config.id;
    item["name"] = lamp.config.name;
    item["enabled"] = lamp.config.enabled;
    item["automation"] = lamp.config.automation;
    item["pending"] = lamp.pending;
    item["requestedOn"] = lamp.requestedOn;
    if (lamp.commandKnown) item["lastCommandOn"] = lamp.lastCommandOn;
    item["httpCode"] = lamp.httpCode;
    item["error"] = lamp.error;
  }
}

void LightingController::pollShellyInputs()
{
  MotionSensorConfig target;
  uint32_t generation = 0;
  uint32_t sequence = 0;
  bool found = false;
  {
    std::lock_guard<std::mutex> lock(motionMutex);
    const unsigned long now = millis();
    if (!motionPollCycle) {
      if (cfg.shellyPollIntervalMs == 0 || isBeforeMillis(now, lastShellyPollMs + cfg.shellyPollIntervalMs)) return;
      lastShellyPollMs = now;
      nextMotionPoll = 0;
      motionPollCycle = true;
    }
    while (nextMotionPoll < motionStates.size()) {
      const MotionState &state = motionStates[nextMotionPoll++];
      if (!state.config.enabled || !state.config.pollingEnabled) continue;
      target = state.config;
      generation = motionGeneration;
      sequence = state.sequence;
      found = true;
      break;
    }
    if (nextMotionPoll >= motionStates.size()) motionPollCycle = false;
  }
  if (!found) return;
  bool active = false;
  const bool ok = queryShellyInputState(target.pollHost, target.pollChannel, active);
  {
    std::lock_guard<std::mutex> lock(motionMutex);
    if (generation != motionGeneration) return;
    for (MotionState &state : motionStates) {
      if (state.config.id != target.id) continue;
      if (!state.pollKnown || state.pollHealthy != ok) {
        appLogf(APP_LOG_LEVEL, "Polling BWM %s: %s", target.name.c_str(), ok ? "OK" : "fehlgeschlagen");
      }
      state.pollKnown = true;
      state.pollHealthy = ok;
      // A newer HTTP event must not be overwritten by an older poll response.
      if (ok && state.sequence == sequence) recordMotion(state, active, "poll");
      break;
    }
  }
}

bool LightingController::queryShellyInputState(const String &ip, uint8_t channel, bool &state) const
{
  if (ip.length() == 0) {
    return false;
  }

  String url = "http://" + ip + "/rpc/Input.GetStatus?id=" + String(channel);
  HTTPClient http;
  http.setConnectTimeout(700);
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

bool LightingController::setSingleLamp(const String &id, bool on)
{
  std::lock_guard<std::mutex> lock(lampMutex);
  for (LampState &lamp : lampStates) {
    if (lamp.config.id == id && lamp.config.enabled) {
      queueLampCommand(lamp, on);
      appLogf(APP_LOG_LEVEL, "Manuell: Lampe %s -> %s angefordert", lamp.config.name.c_str(), on ? "EIN" : "AUS");
      return true;
    }
  }
  appLogf(APP_LOG_LEVEL, "Lampe unbekannt oder deaktiviert: %s", id.c_str());
  return false;
}

void LightingController::setAllLampsManual(bool on)
{
  if (on) {
    forceOnLatch = true;
    shortOverrideUntil = 0;
    suppressMotionUntil = 0;
  } else {
    forceOnLatch = false;
    shortOverrideUntil = 0;
    suppressMotionUntil = 0;
  }

  offDeadline = 0;
  setAllLampRelays(on);
  relayState = on;
  appLogf(APP_LOG_LEVEL, "Manuell: Alle Kanaele %s", on ? "EIN" : "AUS");
}

void LightingController::setAutomationEnabled(bool enabled)
{
  if (cfg.automationEnabled == enabled) {
    return;
  }

  cfg.automationEnabled = enabled;
  offDeadline = 0;
  appLogf(APP_LOG_LEVEL, "Automatikbetrieb %s", enabled ? "AKTIV" : "INAKTIV");
  evaluateLighting();
}