#include <cassert>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

struct String : std::string {
  using std::string::string;
  String() = default;
  String(const std::string &value) : std::string(value) {}
  explicit String(int value) : std::string(std::to_string(value)) {}
  bool isEmpty() const { return empty(); }
};

struct LampConfig {
  String id, name, onUrl, offUrl;
  bool enabled = true;
  bool buttonGuard = false;
};
struct AppConfig { std::vector<LampConfig> lamps; };

static unsigned long clockMs;
static unsigned long millis() { return clockMs; }
static std::vector<String> requests;
static std::map<String, std::deque<int>> responses;
static std::function<void()> duringRequest;
static bool beginOk = true;
static const int APP_LOG_LEVEL = 0;
static const int HTTPC_DISABLE_FOLLOW_REDIRECTS = 0;
static const int HTTPC_ERROR_CONNECTION_REFUSED = -1;
static const int HTTPC_ERROR_SEND_HEADER_FAILED = -2;
static const int HTTPC_ERROR_SEND_PAYLOAD_FAILED = -3;
static const int HTTPC_ERROR_NOT_CONNECTED = -4;
static const int HTTPC_ERROR_CONNECTION_LOST = -5;
static const int HTTPC_ERROR_READ_TIMEOUT = -11;
static void appLogf(int, const char *, ...) {}

struct HTTPClient {
  String url;
  void setConnectTimeout(int) {}
  void setTimeout(int) {}
  void setFollowRedirects(int) {}
  bool begin(const String &value) { url = value; return beginOk; }
  int GET() {
    requests.push_back(url);
    auto callback = std::move(duringRequest);
    duringRequest = {};
    if (callback) callback();
    auto &codes = responses[url];
    if (codes.empty()) return 200;
    const int code = codes.front();
    codes.pop_front();
    return code;
  }
  void end() {}
  static String errorToString(int code) { return String(code); }
};

#include "controller_test.inc"

static LampConfig lamp(const char *id) {
  LampConfig value;
  value.id = id;
  value.name = id;
  value.onUrl = String(id) + "/on";
  value.offUrl = String(id) + "/off";
  return value;
}

static void reset() {
  clockMs = 100;
  requests.clear();
  responses.clear();
  duringRequest = {};
  beginOk = true;
}

int main() {
  static_assert(sizeof(unsigned long) == 4, "Tests require the ESP32's 32-bit unsigned long (Windows native target).");
  reset();
  {
    AppConfig config{{lamp("a"), lamp("b")}};
    LightingController c(config);
    c.resetLampCommands();
    responses["a/on"] = {-1, -1, -1};
    c.queueLampCommand(c.lampStates[0], true);
    c.queueLampCommand(c.lampStates[1], true);
    c.processLampCommand();
    assert(c.lampStates[0].pending);
    c.processLampCommand();
    assert(requests.back() == "b/on");
    clockMs = 599;
    c.processLampCommand();
    assert(requests.size() == 2);
    clockMs = 600;
    c.processLampCommand();
    assert(requests.size() == 3 && c.lampStates[0].pending);
    clockMs = 2099;
    c.processLampCommand();
    assert(requests.size() == 3);
    clockMs = 2100;
    c.processLampCommand();
    assert(requests.size() == 4 && !c.lampStates[0].pending);
    assert(c.lampStates[0].httpCode == -1 && !c.lampStates[0].error.empty());
    assert(c.lampCommands.empty());
    c.processLampCommand();
    assert(requests.size() == 4);
  }
  std::puts("PASS: exact 500/1500 ms delays, three attempts, other lamps continue, final error");

  for (int failure : {-1, -2, -3, -4, -5, -11}) {
    reset();
    AppConfig config{{lamp("a")}};
    LightingController c(config);
    c.resetLampCommands();
    responses["a/off"] = {failure, 200};
    c.queueLampCommand(c.lampStates[0], false);
    c.processLampCommand();
    clockMs += 500;
    c.processLampCommand();
    assert(requests.size() == 2 && !c.lampStates[0].pending);
    assert(c.lampStates[0].error.empty() && c.lampStates[0].httpCode == 200);
  }
  std::puts("PASS: connection failures retry and success clears error/pending");

  for (int failure : {400, 401, 403, 404, 500, 503, -6, -7, -8, -9, -10}) {
    reset();
    AppConfig config{{lamp("a")}};
    LightingController c(config);
    c.resetLampCommands();
    responses["a/on"] = {failure};
    c.queueLampCommand(c.lampStates[0], true);
    c.processLampCommand();
    assert(requests.size() == 1 && c.lampCommands.empty() && !c.lampStates[0].pending);
  }
  std::puts("PASS: HTTP responses and non-connection errors do not retry");

  reset();
  {
    AppConfig config{{lamp("a")}};
    LightingController c(config);
    c.resetLampCommands();
    beginOk = false;
    c.queueLampCommand(c.lampStates[0], true);
    c.processLampCommand();
    assert(requests.empty() && c.lampCommands.empty() && !c.lampStates[0].pending);
    assert(c.lampStates[0].error == "HTTP-URL konnte nicht initialisiert werden");
  }
  std::puts("PASS: URL initialization errors are explicit and not retried");

  reset();
  {
    AppConfig config{{lamp("a")}};
    LightingController c(config);
    c.resetLampCommands();
    responses["a/on"] = {-1};
    c.queueLampCommand(c.lampStates[0], true);
    c.processLampCommand();
    c.queueLampCommand(c.lampStates[0], false);
    c.processLampCommand();
    clockMs += 5000;
    c.processLampCommand();
    assert((requests == std::vector<String>{"a/on", "a/off"}));
    assert(!c.lampStates[0].pending && !c.lampStates[0].lastCommandOn);
  }
  std::puts("PASS: a newer OFF replaces delayed ON immediately and resets retry budget");

  reset();
  {
    AppConfig config{{lamp("a")}};
    LightingController c(config);
    c.resetLampCommands();
    responses["a/on"] = {-1};
    c.queueLampCommand(c.lampStates[0], true);
    duringRequest = [&] { c.queueLampCommand(c.lampStates[0], false); };
    c.processLampCommand();
    assert(c.lampCommands.size() == 1 && !c.lampCommands.front().on);
    c.processLampCommand();
    assert((requests == std::vector<String>{"a/on", "a/off"}));
  }
  std::puts("PASS: an in-flight failure cannot retry after a newer OFF");

  reset();
  {
    AppConfig config{{lamp("a")}};
    LightingController c(config);
    c.resetLampCommands();
    responses["a/on"] = {-1, -1};
    responses["a/off"] = {-1, -1, 200};
    c.queueLampCommand(c.lampStates[0], true);
    c.processLampCommand();
    clockMs += 500;
    c.processLampCommand();
    c.queueLampCommand(c.lampStates[0], false);
    c.processLampCommand();
    clockMs += 500;
    c.processLampCommand();
    clockMs += 1500;
    c.processLampCommand();
    assert((requests == std::vector<String>{"a/on", "a/on", "a/off", "a/off", "a/off"}));
    assert(c.lampCommands.empty() && c.lampStates[0].error.empty());
  }
  std::puts("PASS: a new target receives a full fresh retry budget");

  reset();
  {
    AppConfig config{{lamp("a"), lamp("b")}};
    LightingController c(config);
    c.resetLampCommands();
    responses["a/off"] = {-1, -1, -1};
    c.queueLampCommand(c.lampStates[0], false, true);
    c.queueLampCommand(c.lampStates[0], true);
    c.queueLampCommand(c.lampStates[1], true);
    c.processLampCommand();
    c.processLampCommand();
    assert(requests.back() == "b/on");
    c.processLampCommand();
    assert(requests.size() == 2);
    clockMs += 500;
    c.processLampCommand();
    clockMs += 1500;
    c.processLampCommand();
    assert(c.lampStates[0].pending);
    c.processLampCommand();
    assert((requests == std::vector<String>{"a/off", "b/on", "a/off", "a/off", "a/on"}));
    assert(!c.lampStates[0].pending);
  }
  std::puts("PASS: startup OFF attempts finish before ON, without blocking other lamps");

  reset();
  {
    AppConfig config{{lamp("a")}};
    LightingController c(config);
    c.resetLampCommands();
    responses["a/on"] = {-1};
    c.queueLampCommand(c.lampStates[0], true);
    duringRequest = [&] { config.lamps.clear(); c.resetLampCommands(); };
    c.processLampCommand();
    c.processLampCommand();
    clockMs += 5000;
    c.processLampCommand();
    assert((requests == std::vector<String>{"a/on", "a/off"}));
  }
  std::puts("PASS: config generation invalidates stale retries and cleans up old OFF target");

  reset();
  {
    AppConfig config{{lamp("a")}};
    LightingController c(config);
    c.resetLampCommands();
    config.lamps.clear();
    c.resetLampCommands();
    responses["a/off"] = {-1, 200};
    c.processLampCommand();
    c.resetLampCommands();
    clockMs += 500;
    c.processLampCommand();
    assert((requests == std::vector<String>{"a/off", "a/off"}));
    assert(c.lampCommands.empty());
  }
  std::puts("PASS: removed-lamp cleanup OFF retains its retry across configuration resets");

  reset();
  {
    AppConfig config{{lamp("a"), lamp("b")}};
    LightingController c(config);
    c.resetLampCommands();
    config.lamps[0].onUrl = "a/new-on";
    c.resetLampCommands();
    responses["a/off"] = {-1, 200};
    c.queueLampCommand(c.lampStates[0], false, true);
    c.queueLampCommand(c.lampStates[0], true);
    c.queueLampCommand(c.lampStates[1], true);
    c.processLampCommand();
    c.processLampCommand();
    assert(requests.back() == "b/on");
    c.processLampCommand();
    assert(requests.size() == 2);
    clockMs += 500;
    c.processLampCommand();
    c.processLampCommand();
    c.processLampCommand();
    assert((requests == std::vector<String>{"a/off", "b/on", "a/off", "a/off", "a/new-on"}));
  }
  std::puts("PASS: old-target cleanup cannot retry OFF after ON to a shared reassigned target");

  reset();
  {
    AppConfig config{{lamp("a")}};
    LightingController c(config);
    c.resetLampCommands();
    responses["a/on"] = {-1, 200};
    clockMs = 0xffffffffUL - 499;
    c.queueLampCommand(c.lampStates[0], true);
    c.processLampCommand();
    clockMs = 0xffffffffUL;
    c.processLampCommand();
    assert(requests.size() == 1);
    clockMs = 0;
    c.processLampCommand();
    assert(requests.size() == 2 && c.lampCommands.empty());
  }
  std::puts("PASS: retry deadline works across millis rollover including deadline zero");

  reset();
  {
    AppConfig config{{lamp("a")}};
    config.lamps[0].buttonGuard = true;
    LightingController c(config);
    c.resetLampCommands();
    responses["a/on"] = {-1, 200};
    c.queueLampCommand(c.lampStates[0], true);
    c.processLampCommand();
    assert(c.hasShelly102Command && c.lastShelly102CommandMs == clockMs);
    clockMs += 500;
    c.processLampCommand();
    assert(c.lastShelly102CommandMs == clockMs);
  }
  std::puts("PASS: each retry refreshes the guarded lamp's button feedback window");
}
