#include "debug_log.h"

#include <Det.hpp>
#include <stdarg.h>
#include <time.h>

namespace {

static const size_t LOG_CAPACITY = 120;
String logBuffer[LOG_CAPACITY];
size_t logStart = 0;
size_t logCount = 0;

void pushLogLine(const String &line)
{
  if (logCount < LOG_CAPACITY) {
    logBuffer[(logStart + logCount) % LOG_CAPACITY] = line;
    logCount++;
    return;
  }

  logBuffer[logStart] = line;
  logStart = (logStart + 1) % LOG_CAPACITY;
}

} // namespace

void appLogf(int level, const char *format, ...)
{
  char msg[220];
  va_list args;
  va_start(args, format);
  vsnprintf(msg, sizeof(msg), format, args);
  va_end(args);

  _DetPrint(level, "%s", msg);

  String stamp;
  const time_t now = time(nullptr);
  if (now > 1700000000) {
    struct tm tmNow;
    char timeBuf[32];
    if (localtime_r(&now, &tmNow) != nullptr && strftime(timeBuf, sizeof(timeBuf), "%d.%m.%Y %H:%M:%S", &tmNow) > 0) {
      stamp = String(timeBuf);
    }
  }

  if (stamp.length() == 0) {
    stamp = String("+") + String(millis()) + " ms";
  }

  String line = "[" + stamp + "] " + String(msg);
  pushLogLine(line);
}

String getDebugLogText()
{
  String out;
  for (size_t i = 0; i < logCount; i++) {
    size_t idx = (logStart + i) % LOG_CAPACITY;
    out += logBuffer[idx];
    out += "\n";
  }
  return out;
}

void clearDebugLog()
{
  logStart = 0;
  logCount = 0;
}