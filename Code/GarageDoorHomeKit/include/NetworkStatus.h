// NetworkStatus.h
// Read-only view of the Wi-Fi station interface.

#pragma once

#include <Arduino.h>

class NetworkStatus {
public:
  static bool hasIp();
  static String ipAddress();   // "" if no address
  static String ssid();        // "" if not associated
  static int rssi();           // dBm, 0 if not associated
};
