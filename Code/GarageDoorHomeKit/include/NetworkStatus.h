// NetworkStatus.h
// Read-only view of the Wi-Fi station interface that HomeSpan brought up.
// Uses ESP-IDF directly, so it works whichever library manages Wi-Fi
// (and doesn't disturb it).

#pragma once

#include <Arduino.h>

class NetworkStatus {
public:
  static bool hasIp();
  static String ipAddress();   // "" if no address
  static String ssid();        // "" if not associated
  static int rssi();           // dBm, 0 if not associated
};
