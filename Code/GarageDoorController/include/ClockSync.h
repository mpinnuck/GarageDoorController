// ClockSync.h
// Wall-clock time from internet NTP servers, in the local time zone.
// Start it once the device has an IP address.

#pragma once

#include <Arduino.h>
#include <time.h>

class ClockSync {
public:
  /**
   * @brief Starts NTP time synchronization with the given local time zone.
   * @param[in] posixTz POSIX time-zone string used to configure local time.
   */
  void begin(const char *posixTz);

  /**
   * @brief Checks whether time synchronization has been started.
   * @return True if begin() has been called; otherwise, false.
   */
  bool started() const { return _started; }

  /**
   * @brief Checks whether NTP has set the system clock.
   * @return True if the clock is synchronized; otherwise, false.
   */
  bool isSynced() const;

  /**
   * @brief Gets the current system time.
   * @return Seconds since the Unix epoch, or zero if the clock is not synchronized.
   */
  time_t now() const;

private:
  bool _started = false;
};
