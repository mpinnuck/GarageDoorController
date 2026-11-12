// StatusWebServer.h
// Web UI on port 80: a status page plus JSON endpoints.
//   GET  /                     status page (StatusPage.h)
//   GET  /api/status           current state as JSON
//   GET  /api/log?since=N      log entries newer than sequence N as JSON
//   POST /api/reset-boot-count sets the stored boot count back to 0
//   POST /api/door/press      presses the opener's wall-button input
//   POST /api/door/control    enables or disables opener control
// Starts itself once the device has an IP address. Call loop() from loop().

#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "BootInfo.h"
#include "ClockSync.h"
#include "DeviceStatus.h"
#include "EventLog.h"

class StatusWebServer {
public:
  StatusWebServer(EventLog &log, const ClockSync &clock, BootInfo &boot,
                  const DeviceStatus &status, bool (*pressDoor)(),
                  bool (*setControlEnabled)(bool));

  void loop();
  bool running() const { return _running; }

private:
  void handleRoot();
  void handleStatus();
  void handleLog();
  void handleResetBootCount();
  void handleDoorPress();
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
  bool (*_setControlEnabled)(bool);
};
