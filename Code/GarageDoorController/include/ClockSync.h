// ClockSync.h
// Wall-clock time from internet NTP servers, in the local time zone.
// Start it once the device has an IP address.

#pragma once

#include <Arduino.h>
#include <time.h>

class ClockSync {
public:
  // POSIX TZ string, e.g. Sydney: "AEST-10AEDT,M10.1.0,M4.1.0/3"
  void begin(const char *posixTz);

  bool started() const { return _started; }

  // True once NTP has set the clock.
  bool isSynced() const;

  // Seconds since 1970 (UTC), or 0 if not yet synced.
  time_t now() const;

private:
  bool _started = false;
};
