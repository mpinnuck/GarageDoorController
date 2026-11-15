#pragma once

#include <functional>
#include <map>
#include <set>
#include <string>
#include "Arduino.h"

enum HTTPMethod { HTTP_GET, HTTP_POST };

class WebServer {
public:
  explicit WebServer(int) { instance = this; }
  void collectHeaders(const char **headers, int count) {
    for (int i = 0; i < count; ++i) collectedHeaders.insert(headers[i]);
  }
  void on(const char *path, HTTPMethod method, std::function<void()> handler) {
    routes[{path, method}] = handler;
  }
  void onNotFound(std::function<void()>) {}
  void begin() {}
  void handleClient() {}
  bool hasArg(const char *) const { return false; }
  String arg(const char *) const { return body.c_str(); }
  String header(const char *name) const {
    if (!collectedHeaders.count(name)) return "";
    const auto it = requestHeaders.find(name);
    return it == requestHeaders.end() ? "" : it->second.c_str();
  }
  void sendHeader(const char *, const char *) {}
  void send_P(int code, const char *type, const char *response) { send(code, type, response); }
  void send(int code, const char *type, const String &response) {
    status = code;
    contentType = type;
    responseBody = response.c_str();
  }
  void request(const char *path, const char *headerValue, const char *requestBody,
               const char *token = "test-control-token") {
    requestHeaders["X-Garage-Control"] = headerValue;
    requestHeaders["X-Garage-Token"] = token;
    body = requestBody;
    routes.at({path, HTTP_POST})();
  }
  void get(const char *path) { routes.at({path, HTTP_GET})(); }

  static WebServer *instance;
  int status = 0;
  std::string contentType;
  std::string responseBody;
private:
  std::map<std::pair<std::string, HTTPMethod>, std::function<void()>> routes;
  std::set<std::string> collectedHeaders;
  std::map<std::string, std::string> requestHeaders;
  std::string body;
};
