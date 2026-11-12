// StatusWebServer.h
// Web UI on port 80: a status page plus JSON endpoints.
//   GET  /                     status page (StatusPage.h)
//   GET  /api/status           current state as JSON
//   GET  /api/log?since=N      log entries newer than sequence N as JSON
//   POST /api/reset-boot-count sets the stored boot count back to 0
//   POST /api/door/press      presses the opener's wall-button input
//   POST /api/door/open|close|stop conditionally presses the opener input
//   POST /api/door/control    enables or disables opener control
// Starts itself once the device has an IP address. Call loop() from loop().

#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "BootInfo.h"
#include "ClockSync.h"
#include "DeviceStatus.h"
#include "DoorWebCommandPolicy.h"
#include "EventLog.h"

class StatusWebServer {
public:
  StatusWebServer(EventLog &log, const ClockSync &clock, BootInfo &boot,
                  const DeviceStatus &status, bool (*pressDoor)(),
                  DoorWebCommandPolicy::Result (*commandDoor)(DoorWebCommandPolicy::Command),
                  bool (*setControlEnabled)(bool));

  void loop();
  bool running() const { return _running; }

private:
  void handleRoot();
  void handleStatus();
  void handleLog();
  void handleResetBootCount();
  void handleDoorPress();
  void handleDoorCommand(DoorWebCommandPolicy::Command command, const char *token);
  void handleControlEnabled();
  void handleNotFound();

  static void appendJsonString(String &out, const char *s);

  WebServer _server{80};
  bool _running = false;

  EventLog &_log;
  const ClockSync &_clock;
  BootInfo &_boot;
  const DeviceStatus &_status;
  bool (*_pressDoor)();
  DoorWebCommandPolicy::Result (*_commandDoor)(DoorWebCommandPolicy::Command);
  bool (*_setControlEnabled)(bool);
};
