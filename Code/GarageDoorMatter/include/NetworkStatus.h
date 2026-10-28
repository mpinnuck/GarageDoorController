// NetworkStatus.h
// Read-only view of the Wi-Fi station interface that Matter brought up.
// Uses ESP-IDF directly: Matter owns Wi-Fi, so the Arduino WiFi object
// does not reflect its state.

#pragma once

#include <Arduino.h>

class NetworkStatus {
public:
  static bool hasIp();
  static String ipAddress();   // "" if no address
  static String ssid();        // "" if not associated
  static int rssi();           // dBm, 0 if not associated
};
