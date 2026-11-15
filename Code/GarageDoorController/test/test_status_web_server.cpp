#include "StatusWebServer.h"
#include "NetworkStatus.h"

#include <cassert>
#include <cstdio>

FakeESP ESP;
WebServer *WebServer::instance = nullptr;
static int resets = 0;
static int controlChanges = 0;

bool NetworkStatus::hasIp() { return true; }
String NetworkStatus::ipAddress() { return "192.0.2.1"; }
String NetworkStatus::ssid() { return "test"; }
int NetworkStatus::rssi() { return -60; }
time_t ClockSync::now() const { return 0; }
const char *BootInfo::resetReason() const { return "test"; }
void BootInfo::resetCount() { ++resets; }
void EventLog::log(const char *, ...) {}
uint32_t EventLog::latestSeq() const { return 0; }
void EventLog::forEachSince(uint32_t, const std::function<void(const Entry &)> &) const {}

static GarageDoorModel door(true);
static int presses = 0;
static bool pressSucceeds = true;
static bool pressDoor() { ++presses; return true; }
static bool setControlEnabled(bool) { ++controlChanges; return true; }
static DoorWebCommandPolicy::Result commandDoor(DoorWebCommandPolicy::Command command) {
  const auto result = DoorWebCommandPolicy::evaluate(door, command);
  if (result.action == DoorWebCommandPolicy::Action::Press) {
    if (!pressSucceeds) return {DoorWebCommandPolicy::Action::Failed, "opener press failed"};
    pressDoor();
  }
  return result;
}

int main() {
  EventLog log;
  ClockSync clock;
  BootInfo boot;
  DeviceStatus status;
  status.controlEnabled = true;
  StatusWebServer server(log, clock, boot, status, "test-control-token", pressDoor, commandDoor,
                         setControlEnabled);
  server.loop();
  WebServer &http = *WebServer::instance;

  http.get("/");
  assert(http.status == 200);
  assert(http.responseBody.find("test-control-token") == std::string::npos);
  http.get("/api/status");
  assert(http.status == 200);
  assert(http.responseBody.find("test-control-token") == std::string::npos);

  struct Request { const char *path; const char *header; const char *body; };
  const Request protectedRequests[] = {
      {"/api/door/press", "press", "press"},
      {"/api/door/open", "open", "open"},
      {"/api/door/close", "close", "close"},
      {"/api/door/stop", "stop", "stop"},
      {"/api/door/control", "control", "enable"},
      {"/api/reset-boot-count", "reset", "reset"}};
  for (const auto &request : protectedRequests) {
    for (const char *token : {"", "wrong-token"}) {
      http.request(request.path, request.header, request.body, token);
      assert(http.status == 401);
      assert(presses == 0 && controlChanges == 0 && resets == 0);
    }
  }
  http.request("/api/reset-boot-count", "wrong", "reset");
  assert(http.status == 400 && resets == 0);
  http.request("/api/reset-boot-count", "reset", "reset");
  assert(http.status == 200 && resets == 1);
  http.request("/api/door/control", "control", "enable");
  assert(http.status == 200 && controlChanges == 1);

  door.begin(false, 0);
  http.request("/api/door/close", "close", "close");
  assert(http.status == 200);
  assert(http.contentType == "application/json");
  assert(http.responseBody == "{\"ok\":true,\"pressed\":true}");
  assert(presses == 1);
  http.request("/api/door/open", "open", "open");
  assert(http.responseBody ==
         "{\"ok\":true,\"pressed\":false,\"reason\":\"already open\"}");
  assert(presses == 1);
  http.request("/api/door/stop", "stop", "stop");
  assert(http.responseBody == "{\"ok\":true,\"pressed\":false,\"reason\":\"already stopped\"}");

  door.update(true, false, 100);
  http.request("/api/door/close", "close", "close");
  assert(http.status == 200);
  assert(http.responseBody == "{\"ok\":true,\"pressed\":false,\"reason\":\"already closed\"}");
  assert(presses == 1);
  http.request("/api/door/open", "open", "open");
  assert(http.status == 200);
  assert(http.responseBody == "{\"ok\":true,\"pressed\":true}");
  assert(presses == 2);
  pressSucceeds = false;
  http.request("/api/door/open", "open", "open");
  assert(http.status == 409);
  assert(presses == 2);
  pressSucceeds = true;

  door.begin(false, 200);
  const GarageDoorModel::Position unconfirmed = {
      1, static_cast<uint8_t>(GarageDoorModel::State::Stopped),
      static_cast<uint8_t>(GarageDoorModel::State::Closing), 0, 1};
  assert(door.restorePosition(unconfirmed));
  http.request("/api/door/open", "open", "open");
  assert(http.status == 200);
  assert(http.responseBody ==
         "{\"ok\":false,\"pressed\":false,\"reason\":\"door direction unconfirmed\"}");
  assert(presses == 2);

  http.request("/api/door/close", "wrong", "close");
  assert(http.status == 400);
  assert(presses == 2);
  status.controlEnabled = false;
  http.request("/api/door/open", "open", "open");
  assert(http.status == 403);
  assert(presses == 2);

  status.controlEnabled = true;
  door.begin(true, 200);
  assert(door.press(300));
  door.update(false, false, 400);
  http.request("/api/door/stop", "stop", "stop");
  assert(http.status == 200);
  assert(http.responseBody ==
         "{\"ok\":false,\"pressed\":false,\"reason\":\"opener start or stop awaiting confirmation\"}");
  assert(presses == 2);
  door.update(false, true, 500);
  http.request("/api/door/stop", "stop", "stop");
  assert(http.status == 200);
  assert(http.responseBody == "{\"ok\":true,\"pressed\":true}");
  assert(presses == 3);
  assert(door.press(600));
  http.request("/api/door/stop", "stop", "stop");
  assert(http.status == 200);
  assert(http.responseBody ==
         "{\"ok\":false,\"pressed\":false,\"reason\":\"opener start or stop awaiting confirmation\"}");
  assert(presses == 3);

  http.request("/api/door/press", "press", "press");
  assert(http.status == 200);
  assert(presses == 4);

  StatusWebServer emptyTokenServer(log, clock, boot, status, "", pressDoor, commandDoor,
                                  setControlEnabled);
  emptyTokenServer.loop();
  WebServer::instance->request("/api/door/press", "press", "press", "");
  assert(WebServer::instance->status == 401);
  assert(presses == 4);

  std::puts("Status web server tests passed");
}
