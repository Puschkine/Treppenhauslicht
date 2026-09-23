#pragma once

#include <Arduino.h>

static const int APP_LOG_LEVEL = 1;

void appLogf(int level, const char *format, ...);
String getDebugLogText();
void clearDebugLog();