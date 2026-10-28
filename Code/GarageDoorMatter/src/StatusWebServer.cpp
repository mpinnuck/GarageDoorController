// StatusWebServer.cpp

#include "StatusWebServer.h"

#include <Matter.h>

#include "NetworkStatus.h"
#include "StatusPage.h"

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

StatusWebServer::StatusWebServer(EventLog &log, const ClockSync &clock, BootInfo &boot,
                                 const DeviceStatus &status)
    : _log(log), _clock(clock), _boot(boot), _status(status) {}

void StatusWebServer::loop() {
  if (!_running) {
    if (!NetworkStatus::hasIp()) return;

    _server.on("/", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/api/status", HTTP_GET, [this]() { handleStatus(); });
    _server.on("/api/log", HTTP_GET, [this]() { handleLog(); });
    _server.on("/api/reset-boot-count", HTTP_POST, [this]() { handleResetBootCount(); });
    _server.onNotFound([this]() { handleNotFound(); });
    _server.begin();
    _running = true;
    _log.log("Web UI started: http://%s/", NetworkStatus::ipAddress().c_str());
  }
  _server.handleClient();
}

void StatusWebServer::handleRoot() {
  _server.sendHeader("Cache-Control", "no-store");
  _server.send_P(200, "text/html", kStatusPage);
}

void StatusWebServer::handleStatus() {
  const uint32_t nowMs = millis();
  String json;
  json.reserve(512);

  json += "{\"doorClosed\":";
  json += _status.doorClosed ? "true" : "false";
  json += ",\"doorChanges\":";
  json += _status.doorChanges;
  json += ",\"lastChangeAgoS\":";
  json += _status.hasChanged ? String((nowMs - _status.lastChangeMs) / 1000) : String(-1);
  json += ",\"uptimeMs\":";
  json += nowMs;
  json += ",\"epoch\":";
  json += String((uint32_t)_clock.now());
  json += ",\"wifi\":";
  json += Matter.isWiFiConnected() ? "true" : "false";
  json += ",\"ssid\":";
  appendJsonString(json, NetworkStatus::ssid().c_str());
  json += ",\"rssi\":";
  json += NetworkStatus::rssi();
  json += ",\"ip\":";
  appendJsonString(json, NetworkStatus::ipAddress().c_str());
  json += ",\"commissioned\":";
  json += Matter.isDeviceCommissioned() ? "true" : "false";
  json += ",\"bootCount\":";
  json += _boot.bootCount();
  json += ",\"resetReason\":";
  appendJsonString(json, _boot.resetReason());
  json += ",\"freeHeap\":";
  json += ESP.getFreeHeap();
  json += ",\"minFreeHeap\":";
  json += ESP.getMinFreeHeap();
  json += ",\"firmware\":";
  appendJsonString(json, FIRMWARE_VERSION " (" __DATE__ " " __TIME__ ")");
  json += "}";

  _server.sendHeader("Cache-Control", "no-store");
  _server.send(200, "application/json", json);
}

void StatusWebServer::handleLog() {
  uint32_t since = 0;
  if (_server.hasArg("since")) {
    since = strtoul(_server.arg("since").c_str(), nullptr, 10);
  }

  String json;
  json.reserve(1024);
  json += "{\"latest\":";
  json += _log.latestSeq();
  json += ",\"now\":";
  json += millis();
  json += ",\"epoch\":";
  json += String((uint32_t)_clock.now());
  json += ",\"entries\":[";

  bool first = true;
  _log.forEachSince(since, [&](const EventLog::Entry &e) {
    if (!first) json += ',';
    first = false;
    json += "{\"s\":";
    json += e.seq;
    json += ",\"t\":";
    json += e.uptimeMs;
    json += ",\"m\":";
    appendJsonString(json, e.text);
    json += '}';
  });
  json += "]}";

  _server.sendHeader("Cache-Control", "no-store");
  _server.send(200, "application/json", json);
}

void StatusWebServer::handleResetBootCount() {
  _boot.resetCount();
  _log.log("Boot count reset from web UI");
  _server.sendHeader("Cache-Control", "no-store");
  _server.send(200, "application/json", "{\"ok\":true}");
}

void StatusWebServer::handleNotFound() {
  _server.send(404, "text/plain", "Not found");
}

void StatusWebServer::appendJsonString(String &out, const char *s) {
  out += '"';
  for (; *s; ++s) {
    const char c = *s;
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if ((uint8_t)c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  out += '"';
}
