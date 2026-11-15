// EventLog.h
// Fixed-size ring buffer of log lines, mirrored to Serial.
// Thread-safe: may be called from any task.

#pragma once

#include <Arduino.h>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class EventLog {
public:
  static constexpr size_t kCapacity = 100;   // lines kept in RAM
  static constexpr size_t kTextLen = 120;    // max characters per line

  struct Entry {
    uint32_t seq;        // increasing sequence number, never reused
    uint32_t uptimeMs;   // millis() when logged
    char text[kTextLen];
  };

  /**
   * @brief Attempts to initialize the mutex used to protect the log.
   */
  void begin();

  /**
   * @brief Formats a message and writes it to Serial and the ring buffer.
   * @param[in] fmt printf-style format string followed by its format arguments.
   */
  void log(const char *fmt, ...) __attribute__((format(printf, 2, 3)));

  /**
   * @brief Gets the sequence number of the newest log entry.
   * @return The newest sequence number, or zero if the log is empty.
   */
  uint32_t latestSeq() const;

  /**
   * @brief Calls a function for each stored entry newer than a sequence number.
   * @param[in] since Sequence number after which to return entries.
   * @param[in] fn Function called with each matching entry, oldest first.
   *
   * The callback runs while the log mutex is held and must not call this log.
   */
  void forEachSince(uint32_t since, const std::function<void(const Entry &)> &fn) const;

private:
  /**
   * @brief Acquires the log mutex when it has been initialized.
   */
  void lock() const;

  /**
   * @brief Releases the log mutex when it has been initialized.
   */
  void unlock() const;

  Entry _entries[kCapacity] = {};
  uint32_t _nextSeq = 1;
  SemaphoreHandle_t _mutex = nullptr;
};

extern EventLog Log;
