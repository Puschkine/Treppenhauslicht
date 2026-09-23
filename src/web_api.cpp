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
  doc["ip"] = WiFi.localIP().toString();
  doc["rssi"] = WiFi.RSSI();
  doc["wifi_status"] = getConnectionState();
  doc["relayState"] = controller.getRelayState();
  doc["motionEsp"] = controller.getMotionEsp();
  doc["motion103"] = controller.getMotion103();
  doc["motion104"] = controller.getMotion104();
  doc["activeMotionCount"] = controller.activeMotionCount();
  doc["forceOnLatch"] = controller.getForceOnLatch();
  doc["shortOverrideActive"] = controller.shortOverrideActive();
  doc["timeWindowActive"] = controller.isMotionWindowActive();
  doc["timeWindow"] = String(hhmmFromMinutes(cfg.motionStartMin) + "-" + hhmmFromMinutes(cfg.motionEndMin));
  doc["epoch"] = (uint32_t)time(nullptr);

  String payload;
  serializeJson(doc, payload);
  return payload;
}

String buildConfigJson(const AppConfig &cfg)
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
  doc["motionStartMin"] = cfg.motionStartMin;
  doc["motionEndMin"] = cfg.motionEndMin;
  doc["motionStartHHMM"] = hhmmFromMinutes(cfg.motionStartMin);
  doc["motionEndHHMM"] = hhmmFromMinutes(cfg.motionEndMin);
  doc["ntpServer"] = cfg.ntpServer;
  doc["gmtOffsetSec"] = cfg.gmtOffsetSec;
  doc["daylightOffsetSec"] = cfg.daylightOffsetSec;

  String payload;
  serializeJson(doc, payload);
  return payload;
}

void applyJsonConfig(const JsonDocument &doc, AppConfig &cfg)
{
  if (doc["shelly102Ip"].is<String>()) {
    cfg.shelly102Ip = doc["shelly102Ip"].as<String>();
  }
  if (doc["shelly103Ip"].is<String>()) {
    cfg.shelly103Ip = doc["shelly103Ip"].as<String>();
  }
  if (doc["shelly104Ip"].is<String>()) {
    cfg.shelly104Ip = doc["shelly104Ip"].as<String>();
  }
  if (doc["shelly103MotionChannel"].is<int>()) {
    cfg.shelly103MotionChannel = (uint8_t)constrain(doc["shelly103MotionChannel"].as<int>(), 0, 1);
  }
  if (doc["shelly104MotionChannel"].is<int>()) {
    cfg.shelly104MotionChannel = (uint8_t)constrain(doc["shelly104MotionChannel"].as<int>(), 0, 1);
  }
  if (doc["shelly103OutputChannel"].is<int>()) {
    cfg.shelly103OutputChannel = (uint8_t)constrain(doc["shelly103OutputChannel"].as<int>(), 0, 1);
  }
  if (doc["shelly104OutputChannel"].is<int>()) {
    cfg.shelly104OutputChannel = (uint8_t)constrain(doc["shelly104OutputChannel"].as<int>(), 0, 1);
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
  if (doc["shellyPollingEnabled"].is<bool>()) {
    cfg.shellyPollingEnabled = doc["shellyPollingEnabled"].as<bool>();
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
  if (doc["motionStartMin"].is<int>()) {
    cfg.motionStartMin = (uint16_t)constrain(doc["motionStartMin"].as<int>(), 0, 1439);
  }
  if (doc["motionEndMin"].is<int>()) {
    cfg.motionEndMin = (uint16_t)constrain(doc["motionEndMin"].as<int>(), 0, 1439);
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

  server.on("/api/state", HTTP_GET, [&](AsyncWebServerRequest *request) {
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

        applyJsonConfig(doc, cfg);
        saveConfig(cfg);
        applyTimeConfig(cfg);
        controller.onConfigUpdated();
        request->send(200, "application/json", "{\"ok\":true}");
      });

  server.on("/api/event/motion", HTTP_GET, [&](AsyncWebServerRequest *request) {
    String node = request->hasParam("node") ? request->getParam("node")->value() : "";
    if (node.length() == 0 && request->hasParam("device")) {
      node = request->getParam("device")->value();
    }
    if (node.length() == 0 && request->hasParam("sensor")) {
      node = request->getParam("sensor")->value();
    }

    String stateRaw = request->hasParam("state") ? request->getParam("state")->value() : "0";
    bool state = parseBool(stateRaw);
    int channel = request->hasParam("channel") ? request->getParam("channel")->value().toInt() : 0;

    if (node == "103" && channel != cfg.shelly103MotionChannel) {
      request->send(200, "application/json", "{\"ok\":true,\"ignored\":\"channel_mismatch_103\"}");
      return;
    }
    if (node == "104" && channel != cfg.shelly104MotionChannel) {
      request->send(200, "application/json", "{\"ok\":true,\"ignored\":\"channel_mismatch_104\"}");
      return;
    }

    if (node != "esp" && node != "103" && node != "104") {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"unknown_node\"}");
      return;
    }

    controller.onMotionEvent(node, state);
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

    bool fromShelly102 = cfg.shelly102Ip.length() > 0 && remoteIp == cfg.shelly102Ip;
    bool fromUiTest = request->hasParam("source") && request->getParam("source")->value() == "ui";

    appLogf(APP_LOG_LEVEL, "Button Event Request von %s: type='%s' action='%s'",
            remoteIp.c_str(), type.c_str(), actionRaw.c_str());

    if (!fromShelly102 && !fromUiTest) {
      appLogf(APP_LOG_LEVEL, "Button Event verworfen: ungueltige Quelle %s", remoteIp.c_str());
      request->send(403, "application/json", "{\"ok\":false,\"error\":\"forbidden_source\"}");
      return;
    }

    if (fromShelly102 && !fromUiTest && controller.isShelly102CommandRecent(BUTTON_CROSSTALK_GUARD_MS)) {
      appLogf(APP_LOG_LEVEL,
              "Button Event verworfen: plausibles Uebersprechen nach Shelly102-Schalten (%lu ms)",
              BUTTON_CROSSTALK_GUARD_MS);
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

  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
  });
}