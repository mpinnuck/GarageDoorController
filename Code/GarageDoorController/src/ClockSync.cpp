// ClockSync.cpp

#include "ClockSync.h"

// Any time before this means the clock has not been set yet.
static constexpr time_t kValidAfter = 1735689600;  // 2025-01-01

void ClockSync::begin(const char *posixTz) {
  if (_started) return;
  configTzTime(posixTz, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  _started = true;
}

bool ClockSync::isSynced() const {
  return time(nullptr) > kValidAfter;
}

time_t ClockSync::now() const {
  time_t t = time(nullptr);
  return (t > kValidAfter) ? t : 0;
}
