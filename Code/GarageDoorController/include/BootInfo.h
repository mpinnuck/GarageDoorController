// BootInfo.h
// Persistent boot counter and the reason for the last reset,
// so unexpected reboots are visible on the status page.

#pragma once

#include <Arduino.h>
#include <esp_system.h>

class BootInfo {
public:
  /**
   * @brief Reads the reset reason and increments the persisted boot counter.
   */
  void begin();

  /**
   * @brief Sets the in-memory boot counter to zero and attempts to persist it.
   */
  void resetCount();

  /**
   * @brief Gets the boot count for this device.
   * @return The current boot count.
   */
  uint32_t bootCount() const { return _bootCount; }

  /**
   * @brief Gets a human-readable description of the last reset reason.
   * @return A constant string describing the reset reason.
   */
  const char *resetReason() const;

private:
  uint32_t _bootCount = 0;
  esp_reset_reason_t _reason = ESP_RST_UNKNOWN;
};
