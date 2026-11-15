// NetworkStatus.h
// Read-only view of the Wi-Fi station interface.

#pragma once

#include <Arduino.h>

class NetworkStatus {
public:
  /**
   * @brief Checks whether the Wi-Fi station has an IP address.
   * @return True if a nonzero IP address is assigned; otherwise, false.
   */
  static bool hasIp();

  /**
   * @brief Gets the Wi-Fi station's IP address.
   * @return The address as text, or an empty string if unavailable.
   */
  static String ipAddress();

  /**
   * @brief Gets the SSID of the associated access point.
   * @return The SSID, or an empty string if not associated.
   */
  static String ssid();

  /**
   * @brief Gets the received Wi-Fi signal strength.
   * @return RSSI in dBm, or zero if not associated.
   */
  static int rssi();
};
