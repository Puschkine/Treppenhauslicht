#include <Arduino.h>
#include <WiFi.h>
#include <WLANsupport.h>
#include <esp_wifi.h>
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

namespace {

void tuneWifiRadio()
{
  WiFi.setSleep(false);

  const esp_err_t txErr = esp_wifi_set_max_tx_power(78); // 78 * 0.25 dBm = 19.5 dBm
  if (txErr == ESP_OK) {
    appLogf(APP_LOG_LEVEL, "WiFi TX Power auf 19.5 dBm gesetzt");
  } else {
    appLogf(APP_LOG_LEVEL, "WiFi TX Power konnte nicht gesetzt werden (err=%d)", (int)txErr);
  }

  wifi_config_t apCfg;
  memset(&apCfg, 0, sizeof(apCfg));
  const esp_err_t getApErr = esp_wifi_get_config(WIFI_IF_AP, &apCfg);
  if (getApErr == ESP_OK) {
    apCfg.ap.max_connection = 8;
    const esp_err_t setApErr = esp_wifi_set_config(WIFI_IF_AP, &apCfg);
    if (setApErr == ESP_OK) {
      appLogf(APP_LOG_LEVEL, "AP max_connection auf 8 gesetzt");
    } else {
      appLogf(APP_LOG_LEVEL, "AP max_connection konnte nicht gesetzt werden (err=%d)", (int)setApErr);
    }
  } else {
    appLogf(APP_LOG_LEVEL, "AP Konfiguration konnte nicht gelesen werden (err=%d)", (int)getApErr);
  }
}

}

void setup()
{
  Serial.begin(115200);
  delay(300);

  appLogf(APP_LOG_LEVEL, "Starte Treppenhauslicht...");

  initWiFi(true);
  tuneWifiRadio();
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
