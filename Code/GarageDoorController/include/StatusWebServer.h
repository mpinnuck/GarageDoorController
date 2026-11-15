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
  /**
   * @brief Creates the web server and binds it to the device services.
   * @param[in] log Event log used to serve and record events.
   * @param[in] clock Clock used to report synchronized time.
   * @param[in] boot Boot information used by status and reset handlers.
   * @param[in] status Current device status exposed by the API.
   * @param[in] controlToken Token required for control endpoints.
   * @param[in] pressDoor Callback that attempts a raw opener press.
   * @param[in] commandDoor Callback that evaluates and executes a door command.
   * @param[in] setControlEnabled Callback that persists and applies control state.
   */
  StatusWebServer(EventLog &log, const ClockSync &clock, BootInfo &boot,
                  const DeviceStatus &status, const char *controlToken, bool (*pressDoor)(),
                  DoorWebCommandPolicy::Result (*commandDoor)(DoorWebCommandPolicy::Command),
                  bool (*setControlEnabled)(bool));

  /**
   * @brief Starts the server after an IP address is available and handles clients.
   */
  void loop();

  /**
   * @brief Checks whether the HTTP server has been started.
   * @return True if running; otherwise, false.
   */
  bool running() const { return _running; }

private:
  /**
   * @brief Serves the embedded status page.
   */
  void handleRoot();

  /**
   * @brief Serves the current device state as JSON.
   */
  void handleStatus();

  /**
   * @brief Serves log entries newer than the requested sequence number.
   */
  void handleLog();

  /**
   * @brief Authenticates and handles a request to reset the boot counter.
   */
  void handleResetBootCount();

  /**
   * @brief Authenticates and handles a raw opener-button press request.
   */
  void handleDoorPress();

  /**
   * @brief Authenticates and handles a conditional door command.
   * @param[in] command Command requested by the endpoint.
   * @param[in] token Expected endpoint command token.
   */
  void handleDoorCommand(DoorWebCommandPolicy::Command command, const char *token);

  /**
   * @brief Authenticates and handles a request to enable or disable control.
   */
  void handleControlEnabled();

  /**
   * @brief Sends the not-found response for an unknown endpoint.
   */
  void handleNotFound();

  /**
   * @brief Checks the request's control token and sends an error if invalid.
   * @return True if the request is authorized; otherwise, false.
   */
  bool authorizeControl();

  /**
   * @brief Appends a JSON-escaped string value to an output buffer.
   * @param[in,out] out String receiving the quoted JSON value.
   * @param[in] s Null-terminated string to escape.
   */
  static void appendJsonString(String &out, const char *s);

  WebServer _server{80};
  bool _running = false;

  EventLog &_log;
  const ClockSync &_clock;
  BootInfo &_boot;
  const DeviceStatus &_status;
  const char *_controlToken;
  bool (*_pressDoor)();
  DoorWebCommandPolicy::Result (*_commandDoor)(DoorWebCommandPolicy::Command);
  bool (*_setControlEnabled)(bool);
};
