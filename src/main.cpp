#include <Arduino.h>
#include <WiFi.h>
#include <WLANsupport.h>
#include <Det.hpp>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <LittleFS.h>

#include "app_config.h"
#include "debug_log.h"
#include "lighting_controller.h"
#include "web_api.h"

AsyncWebServer server(80);
AppConfig cfg;
LightingController controller(cfg);

void setup()
{
  Serial.begin(115200);
  delay(300);

  appLogf(APP_LOG_LEVEL, "Starte Treppenhauslicht...");

  initWiFi(true);
  printWifiIP();

  if (!LittleFS.begin(true)) {
    appLogf(APP_LOG_LEVEL, "LittleFS konnte nicht gemountet werden");
  } else {
    appLogf(APP_LOG_LEVEL, "LittleFS erfolgreich gemountet");
  }

  loadConfig(cfg);
  applyTimeConfig(cfg);
  controller.begin();

  setupRoutes(server, cfg, controller);
  ElegantOTA.begin(&server);
  server.begin();

  appLogf(APP_LOG_LEVEL, "Async Webserver gestartet auf Port 80");
  appLogf(APP_LOG_LEVEL, "ElegantOTA aktiv unter /update");
}

void loop()
{
  ElegantOTA.loop();
  controller.loop();
  delay(10);
}
