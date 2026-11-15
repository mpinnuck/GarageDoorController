// BootInfo.cpp

#include "BootInfo.h"

#include <Preferences.h>

void BootInfo::begin() {
  _reason = esp_reset_reason();

  Preferences prefs;
  if (prefs.begin("bootinfo", false)) {
    _bootCount = prefs.getUInt("count", 0) + 1;
    prefs.putUInt("count", _bootCount);
    prefs.end();
  }
}

void BootInfo::resetCount() {
  _bootCount = 0;
  Preferences prefs;
  if (prefs.begin("bootinfo", false)) {
    prefs.putUInt("count", 0);
    prefs.end();
  }
}

const char *BootInfo::resetReason() const {
  switch (_reason) {
    case ESP_RST_POWERON:   return "Power on";
    case ESP_RST_EXT:       return "External reset";
    case ESP_RST_SW:        return "Software restart";
    case ESP_RST_PANIC:     return "Crash (panic)";
    case ESP_RST_INT_WDT:   return "Interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "Task watchdog";
    case ESP_RST_WDT:       return "Other watchdog";
    case ESP_RST_DEEPSLEEP: return "Wake from deep sleep";
    case ESP_RST_BROWNOUT:  return "Brownout (power dip)";
    case ESP_RST_SDIO:      return "SDIO reset";
    case ESP_RST_USB:       return "USB reset";
    case ESP_RST_JTAG:      return "JTAG reset";
    default:                return "Unknown";
  }
}
