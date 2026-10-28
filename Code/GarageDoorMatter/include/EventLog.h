// EventLog.h
// Fixed-size ring buffer of log lines, mirrored to Serial.
// Thread-safe: may be called from loop() and from Matter event callbacks.

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

  void begin();

  // printf-style logging. Writes to Serial and the ring buffer.
  void log(const char *fmt, ...) __attribute__((format(printf, 2, 3)));

  // Sequence number of the newest entry (0 if empty).
  uint32_t latestSeq() const;

  // Calls fn for every stored entry with seq > since, oldest first.
  void forEachSince(uint32_t since, const std::function<void(const Entry &)> &fn) const;

private:
  void lock() const;
  void unlock() const;

  Entry _entries[kCapacity] = {};
  uint32_t _nextSeq = 1;
  SemaphoreHandle_t _mutex = nullptr;
};

extern EventLog Log;
