#include <Arduino.h>
#include <WiFi.h>
#include <WLANsupport.h>
#include <Det.hpp>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <LittleFS.h>

AsyncWebServer server(80);

void setupRoutes()
{
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    String payload = "{";
    payload += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    payload += "\"rssi\":" + String(WiFi.RSSI()) + ",";
    payload += "\"wifi_status\":" + String(getConnectionState());
    payload += "}";
    request->send(200, "application/json", payload);
  });

  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
  });
}

void setup()
{
  Serial.begin(115200);
  delay(300);

  _DetPrint(DET_LEV1, "Starte Treppenhauslicht...");
  initWiFi(true);
  printWifiIP();

  if (!LittleFS.begin(true)) {
    _DetPrint(DET_LEV1, "LittleFS konnte nicht gemountet werden");
  } else {
    _DetPrint(DET_LEV1, "LittleFS erfolgreich gemountet");
  }

  setupRoutes();
  ElegantOTA.begin(&server);
  server.begin();

  _DetPrint(DET_LEV1, "Async Webserver gestartet auf Port 80");
  _DetPrint(DET_LEV1, "ElegantOTA aktiv unter /update");
}

void loop()
{
  ElegantOTA.loop();
  delay(10);
}