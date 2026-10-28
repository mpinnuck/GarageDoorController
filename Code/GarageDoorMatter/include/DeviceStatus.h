// DeviceStatus.h
// Door state shared between the main loop (writer) and the web server (reader).
// Both run in the loop() task, so no locking is needed.

#pragma once

#include <Arduino.h>

struct DeviceStatus {
  bool doorClosed = false;
  uint32_t doorChanges = 0;      // open/close transitions since boot
  uint32_t lastChangeMs = 0;     // millis() of the last transition
  bool hasChanged = false;       // false until the first transition
};
