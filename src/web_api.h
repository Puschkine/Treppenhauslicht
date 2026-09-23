#pragma once

#include <ESPAsyncWebServer.h>

#include "app_config.h"
#include "lighting_controller.h"

void setupRoutes(AsyncWebServer &server, AppConfig &cfg, LightingController &controller);