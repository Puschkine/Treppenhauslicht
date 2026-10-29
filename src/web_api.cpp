#include "web_api.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WLANsupport.h>
#include <WiFi.h>
#include <time.h>

#include "debug_log.h"

namespace {

bool parseBool(const String &value)
{
  String v = value;
  v.toLowerCase();
  return v == "1" || v == "true" || v == "on" || v == "yes";
}

String buildStateJson(const AppConfig &cfg, const LightingController &controller)
{
  JsonDocument doc;
  const time_t now = time(nullptr);
  struct tm localTm;
  char localTimeBuf[32] = "unbekannt";
  if (localtime_r(&now, &localTm) != nullptr) {
    strftime(localTimeBuf, sizeof(localTimeBuf), "%d.%m.%Y %H:%M:%S", &localTm);
  }

  doc["ip"] = WiFi.localIP().toString();
  doc["ipSta"] = WiFi.localIP().toString();
  doc["ipAp"] = WiFi.softAPIP().toString();
  doc["rssi"] = WiFi.RSSI();
  doc["wifi_status"] = getConnectionState();
  doc["relayState"] = controller.getRelayState();
  controller.writeLampStatus(doc["lamps"].to<JsonArray>());
  controller.writeMotionStatus(doc["motionSensors"].to<JsonArray>());
  doc["motionEsp"] = controller.getMotionEsp();
  doc["motion103"] = controller.getMotion103();
  doc["motion104"] = controller.getMotion104();
  doc["activeMotionCount"] = controller.activeMotionCount();
  doc["forceOnLatch"] = controller.getForceOnLatch();
  doc["automationEnabled"] = controller.getAutomationEnabled();
  doc["shortOverrideActive"] = controller.shortOverrideActive();
  doc["timeWindowActive"] = controller.isMotionWindowActive();
  uint16_t windowStartMin = 0;
  uint16_t windowEndMin = 0;
  if (controller.getEffectiveWindow(windowStartMin, windowEndMin)) {
    doc["timeWindow"] = String(hhmmFromMinutes(windowStartMin) + "-" + hhmmFromMinutes(windowEndMin));
  } else {
    doc["timeWindow"] = "unbekannt";
  }
  int sunriseMin = 0;
  int sunsetMin = 0;
  if (controller.getSunTimes(sunriseMin, sunsetMin)) {
    doc["sunrise"] = hhmmFromMinutes((uint16_t)sunriseMin);
    doc["sunset"] = hhmmFromMinutes((uint16_t)sunsetMin);
  }
  doc["epoch"] = (uint32_t)now;
  doc["espTimeLocal"] = localTimeBuf;

  String payload;
  serializeJson(doc, payload);
  return payload;
}

String buildConfigJson(const AppConfig &cfg)
{
  JsonDocument doc;
  writeButtonConfig(doc["buttonSources"].to<JsonArray>(), cfg.buttonSources);
  doc["maxButtonSources"] = MAX_BUTTON_SOURCES;
  writeMotionConfig(doc["motionSensors"].to<JsonArray>(), cfg.motionSensors);
  doc["espMotionEnabled"] = cfg.espMotionEnabled;
  doc["maxMotionSensors"] = MAX_MOTION_SENSORS;
  writeLampConfig(doc["lamps"].to<JsonArray>(), cfg.lamps);
  doc["maxLamps"] = MAX_LAMPS;
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
  doc["motionStartHHMM"] = hhmmFromMinutes(cfg.motionStartMin);
  doc["motionEndHHMM"] = hhmmFromMinutes(cfg.motionEndMin);
  doc["windowUseSun"] = cfg.windowUseSun;
  doc["sunOnlineLookup"] = cfg.sunOnlineLookup;
  doc["latitude"] = cfg.latitude;
  doc["longitude"] = cfg.longitude;
  doc["sunriseOffsetMin"] = cfg.sunriseOffsetMin;
  doc["sunsetOffsetMin"] = cfg.sunsetOffsetMin;
  doc["ntpServer"] = cfg.ntpServer;
  doc["gmtOffsetSec"] = cfg.gmtOffsetSec;
  doc["daylightOffsetSec"] = cfg.daylightOffsetSec;

  String payload;
  serializeJson(doc, payload);
  return payload;
}

void applyJsonConfig(const JsonDocument &doc, AppConfig &cfg)
{
  if (doc["espMotionEnabled"].is<bool>()) {
    cfg.espMotionEnabled = doc["espMotionEnabled"].as<bool>();
  }
  if (doc["motionOffDelayMs"].is<unsigned long>()) {
    cfg.motionOffDelayMs = (uint32_t)constrain((long)doc["motionOffDelayMs"].as<unsigned long>(), 100, 600000);
  }
  if (doc["buttonShortOverrideMs"].is<unsigned long>()) {
    cfg.buttonShortOverrideMs = (uint32_t)constrain((long)doc["buttonShortOverrideMs"].as<unsigned long>(), 100, 600000);
  }
  if (doc["motionEventTimeoutMs"].is<unsigned long>()) {
    cfg.motionEventTimeoutMs = (uint32_t)constrain((long)doc["motionEventTimeoutMs"].as<unsigned long>(), 1000, 3600000);
  }
  if (doc["shellyPollIntervalMs"].is<unsigned long>()) {
    cfg.shellyPollIntervalMs = (uint32_t)constrain((long)doc["shellyPollIntervalMs"].as<unsigned long>(), 1000, 60000);
  }
  if (doc["espMotionPin"].is<int>()) {
    cfg.espMotionPin = (uint8_t)constrain(doc["espMotionPin"].as<int>(), 0, 39);
  }
  if (doc["espMotionLedPin"].is<int>()) {
    cfg.espMotionLedPin = (uint8_t)constrain(doc["espMotionLedPin"].as<int>(), 0, 39);
  }
  if (doc["espMotionInvert"].is<bool>()) {
    cfg.espMotionInvert = doc["espMotionInvert"].as<bool>();
  }
  if (doc["timeWindowEnabled"].is<bool>()) {
    cfg.timeWindowEnabled = doc["timeWindowEnabled"].as<bool>();
  }
  if (doc["automationEnabled"].is<bool>()) {
    cfg.automationEnabled = doc["automationEnabled"].as<bool>();
  }
  if (doc["motionStartMin"].is<int>()) {
    cfg.motionStartMin = (uint16_t)constrain(doc["motionStartMin"].as<int>(), 0, 1439);
  }
  if (doc["motionEndMin"].is<int>()) {
    cfg.motionEndMin = (uint16_t)constrain(doc["motionEndMin"].as<int>(), 0, 1439);
  }
  if (doc["windowUseSun"].is<bool>()) {
    cfg.windowUseSun = doc["windowUseSun"].as<bool>();
  }
  if (doc["sunOnlineLookup"].is<bool>()) {
    cfg.sunOnlineLookup = doc["sunOnlineLookup"].as<bool>();
  }
  if (doc["latitude"].is<double>() || doc["latitude"].is<int>()) {
    cfg.latitude = constrain(doc["latitude"].as<double>(), -90.0, 90.0);
  }
  if (doc["longitude"].is<double>() || doc["longitude"].is<int>()) {
    cfg.longitude = constrain(doc["longitude"].as<double>(), -180.0, 180.0);
  }
  if (doc["sunriseOffsetMin"].is<int>()) {
    cfg.sunriseOffsetMin = (int16_t)constrain(doc["sunriseOffsetMin"].as<int>(), -180, 180);
  }
  if (doc["sunsetOffsetMin"].is<int>()) {
    cfg.sunsetOffsetMin = (int16_t)constrain(doc["sunsetOffsetMin"].as<int>(), -180, 180);
  }
  if (doc["ntpServer"].is<String>()) {
    cfg.ntpServer = doc["ntpServer"].as<String>();
  }
  if (doc["gmtOffsetSec"].is<long>()) {
    cfg.gmtOffsetSec = doc["gmtOffsetSec"].as<long>();
  }
  if (doc["daylightOffsetSec"].is<int>()) {
    cfg.daylightOffsetSec = doc["daylightOffsetSec"].as<int>();
  }
}

} // namespace

void setupRoutes(AsyncWebServer &server, AppConfig &cfg, LightingController &controller)
{
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  server.on("/status", HTTP_GET, [&](AsyncWebServerRequest *request) {
    request->send(200, "application/json", buildStateJson(cfg, controller));
  });

  server.on("/api/status", HTTP_GET, [&](AsyncWebServerRequest *request) {
    request->send(200, "application/json", buildStateJson(cfg, controller));
  });

  server.on("/api/logs", HTTP_GET, [&](AsyncWebServerRequest *request) {
    request->send(200, "text/plain; charset=utf-8", getDebugLogText());
  });

  server.on("/api/logs/clear", HTTP_POST, [&](AsyncWebServerRequest *request) {
    clearDebugLog();
    request->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/config", HTTP_GET, [&](AsyncWebServerRequest *request) {
    request->send(200, "application/json", buildConfigJson(cfg));
  });

  server.on(
      "/api/config", HTTP_POST,
      [](AsyncWebServerRequest *request) {},
      nullptr,
      [&](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (total > 64 * 1024) {
          if (index == 0) request->send(413, "application/json", "{\"ok\":false,\"error\":\"Konfiguration ist groesser als 64 KiB\"}");
          return;
        }
        if (index == 0) {
          request->_tempObject = new String();
        }

        String *body = reinterpret_cast<String *>(request->_tempObject);
        body->concat(reinterpret_cast<const char *>(data), len);

        if (index + len != total) {
          return;
        }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, *body);
        delete body;
        request->_tempObject = nullptr;

        if (err) {
          request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_json\"}");
          return;
        }

        AppConfig updated = cfg;
        applyJsonConfig(doc, updated);
        if (!doc["buttonSources"].isUnbound()) {
          String error;
          if (!readButtonConfig(doc["buttonSources"], updated.buttonSources, error)) {
            appLogf(APP_LOG_LEVEL, "Konfiguration abgelehnt: %s", error.c_str());
            JsonDocument response;
            response["ok"] = false;
            response["error"] = error;
            String payload;
            serializeJson(response, payload);
            request->send(400, "application/json", payload);
            return;
          }
        }
        String motionError;
        if (!doc["espMotionEnabled"].isUnbound() && !doc["espMotionEnabled"].is<bool>()) {
          motionError = "espMotionEnabled muss true oder false sein";
        } else if (!doc["motionSensors"].isUnbound()) {
          readMotionConfig(doc["motionSensors"], updated.motionSensors, motionError);
        }
        if (!motionError.isEmpty()) {
          appLogf(APP_LOG_LEVEL, "Konfiguration abgelehnt: %s", motionError.c_str());
          JsonDocument response;
          response["ok"] = false;
          response["error"] = motionError;
          String payload;
          serializeJson(response, payload);
          request->send(400, "application/json", payload);
          return;
        }
        if (!doc["lamps"].isUnbound()) {
          String error;
          if (!readLampConfig(doc["lamps"], updated.lamps, error)) {
            appLogf(APP_LOG_LEVEL, "Konfiguration abgelehnt: %s", error.c_str());
            JsonDocument response;
            response["ok"] = false;
            response["error"] = error;
            String payload;
            serializeJson(response, payload);
            request->send(400, "application/json", payload);
            return;
          }
        }
        if (controller.lampCommandsBusy()) {
          request->send(409, "application/json", "{\"ok\":false,\"error\":\"Lampenbefehle laufen noch. Bitte erneut speichern.\"}");
          return;
        }
        if (!saveConfig(updated)) {
          request->send(500, "application/json", "{\"ok\":false,\"error\":\"Konfiguration konnte nicht gespeichert werden\"}");
          return;
        }
        cfg = std::move(updated);
        applyTimeConfig(cfg);
        controller.onConfigUpdated();
        request->send(200, "application/json", "{\"ok\":true}");
      });

  server.on("/api/event/motion", HTTP_GET, [&](AsyncWebServerRequest *request) {
    const String stateRaw = request->hasParam("state") ? request->getParam("state")->value() : "";
    if (stateRaw != "0" && stateRaw != "1" && stateRaw != "true" && stateRaw != "false" &&
        stateRaw != "on" && stateRaw != "off") {
      appLogf(APP_LOG_LEVEL, "BWM Event verworfen: ungueltiger Zustand '%s'", stateRaw.c_str());
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_state\"}");
      return;
    }
    if (request->hasParam("id")) {
      const String id = request->getParam("id")->value();
      if (!controller.onMotionEvent(id, parseBool(stateRaw))) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"unknown_or_disabled_sensor\"}");
        return;
      }
      request->send(200, "application/json", "{\"ok\":true}");
      return;
    }
    String node = request->hasParam("node") ? request->getParam("node")->value() : "";
    if (node.length() == 0 && request->hasParam("device")) {
      node = request->getParam("device")->value();
    }
    if (node.length() == 0 && request->hasParam("sensor")) {
      node = request->getParam("sensor")->value();
    }

    String id;
    uint8_t expectedChannel = 0;
    if (!controller.resolveMotionSource(node, id, expectedChannel)) {
      appLogf(APP_LOG_LEVEL, "BWM Event verworfen: unbekannte bisherige Quelle '%s'", node.c_str());
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"unknown_node\"}");
      return;
    }
    const String channelRaw = request->hasParam("channel") ? request->getParam("channel")->value() : "0";
    if (node != "esp" && channelRaw != String(expectedChannel)) {
      appLogf(APP_LOG_LEVEL, "BWM Event ignoriert: Kanal passt nicht fuer %s", node.c_str());
      request->send(200, "application/json", "{\"ok\":true,\"ignored\":\"channel_mismatch\"}");
      return;
    }
    if (!controller.onMotionEvent(id, parseBool(stateRaw))) {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"unknown_or_disabled_sensor\"}");
      return;
    }
    request->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/event/button", HTTP_GET, [&](AsyncWebServerRequest *request) {
    const unsigned long BUTTON_CROSSTALK_GUARD_MS = 1500;
    String remoteIp = request->client() ? request->client()->remoteIP().toString() : "unknown";
    String type = request->hasParam("type") ? request->getParam("type")->value() : "";
    String actionRaw = request->hasParam("action") ? request->getParam("action")->value() : "";
    if (type.length() == 0 && request->hasParam("action")) {
      type = request->getParam("action")->value();
    }
    type.toLowerCase();

    const ButtonSourceConfig *source = nullptr;
    for (const ButtonSourceConfig &candidate : cfg.buttonSources) {
      if (candidate.enabled && candidate.ip == remoteIp) {
        source = &candidate;
        break;
      }
    }
    bool fromUiTest = request->hasParam("source") && request->getParam("source")->value() == "ui";

    appLogf(APP_LOG_LEVEL, "Button Event Request von %s: type='%s' action='%s'",
            remoteIp.c_str(), type.c_str(), actionRaw.c_str());

    if (!source && !fromUiTest) {
      appLogf(APP_LOG_LEVEL, "Button Event verworfen: ungueltige Quelle %s", remoteIp.c_str());
      request->send(403, "application/json", "{\"ok\":false,\"error\":\"forbidden_source\"}");
      return;
    }

    if (source && source->buttonGuard && !fromUiTest && controller.isShelly102CommandRecent(BUTTON_CROSSTALK_GUARD_MS)) {
      appLogf(APP_LOG_LEVEL,
              "Button Event von %s verworfen: plausibles Uebersprechen nach geschuetztem Lampenbefehl (%lu ms)",
              source->name.c_str(), BUTTON_CROSSTALK_GUARD_MS);
      request->send(409, "application/json", "{\"ok\":false,\"error\":\"button_crosstalk_guard\"}");
      return;
    }

    if (type.indexOf("long") >= 0) {
      controller.handleLongPush();
      request->send(200, "application/json", "{\"ok\":true,\"mode\":\"long\"}");
      return;
    }

    if (type.indexOf("short") < 0 && type.indexOf("single") < 0) {
      appLogf(APP_LOG_LEVEL, "Button Event verworfen: unbekannte Action '%s'", type.c_str());
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"unknown_button_action\"}");
      return;
    }

    controller.handleShortPush();
    request->send(200, "application/json", "{\"ok\":true,\"mode\":\"short\"}");
  });

  server.on("/api/force/off", HTTP_POST, [&](AsyncWebServerRequest *request) {
    controller.forceOff();
    request->send(200, "application/json", "{\"ok\":true}");
  });

  server.on(
      "/api/automation", HTTP_POST,
      [](AsyncWebServerRequest *request) {},
      nullptr,
      [&](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (index == 0) {
          request->_tempObject = new String();
        }

        String *body = reinterpret_cast<String *>(request->_tempObject);
        body->concat(reinterpret_cast<const char *>(data), len);

        if (index + len != total) {
          return;
        }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, *body);
        delete body;
        request->_tempObject = nullptr;

        if (err || !doc["enabled"].is<bool>()) {
          request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_payload\"}");
          return;
        }

        AppConfig updated = cfg;
        updated.automationEnabled = doc["enabled"].as<bool>();
        if (!saveConfig(updated)) {
          request->send(500, "application/json", "{\"ok\":false,\"error\":\"Konfiguration konnte nicht gespeichert werden\"}");
          return;
        }
        controller.setAutomationEnabled(updated.automationEnabled);
        request->send(200, "application/json", "{\"ok\":true}");
      });

  server.on(
      "/api/manual/relay", HTTP_POST,
      [](AsyncWebServerRequest *request) {},
      nullptr,
      [&](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (index == 0) {
          request->_tempObject = new String();
        }

        String *body = reinterpret_cast<String *>(request->_tempObject);
        body->concat(reinterpret_cast<const char *>(data), len);

        if (index + len != total) {
          return;
        }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, *body);
        delete body;
        request->_tempObject = nullptr;

        if (err || !doc["on"].is<bool>()) {
          request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_payload\"}");
          return;
        }

        const bool on = doc["on"].as<bool>();
        const bool all = doc["all"].is<bool>() && doc["all"].as<bool>();
        if (all) {
          controller.setAllLampsManual(on);
          request->send(202, "application/json", "{\"ok\":true,\"queued\":true,\"scope\":\"all\"}");
          return;
        }

        String id = doc["id"].is<String>() ? doc["id"].as<String>() : "";
        // Existing callers continue to address the migrated default lamps.
        if (id.isEmpty() && doc["node"].is<int>() && doc["channel"].is<int>()) {
          id = "shelly-" + String(doc["node"].as<int>()) + "-" + String(doc["channel"].as<int>());
        }
        if (!controller.setSingleLamp(id, on)) {
          request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_target\"}");
          return;
        }

        request->send(202, "application/json", "{\"ok\":true,\"queued\":true,\"scope\":\"single\"}");
      });

  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
  });
}