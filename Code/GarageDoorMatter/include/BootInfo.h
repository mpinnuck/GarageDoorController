// BootInfo.h
// Persistent boot counter and the reason for the last reset,
// so unexpected reboots are visible on the status page.

#pragma once

#include <Arduino.h>
#include <esp_system.h>

class BootInfo {
public:
  void begin();   // call once in setup(); increments the stored counter
  void resetCount();   // sets the stored counter back to 0

  uint32_t bootCount() const { return _bootCount; }
  const char *resetReason() const;

private:
  uint32_t _bootCount = 0;
  esp_reset_reason_t _reason = ESP_RST_UNKNOWN;
};
