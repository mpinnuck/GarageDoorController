// EventLog.cpp

#include "EventLog.h"

#include <stdarg.h>

EventLog Log;

void EventLog::begin() {
  if (_mutex == nullptr) {
    _mutex = xSemaphoreCreateMutex();
  }
}

void EventLog::lock() const {
  if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
}

void EventLog::unlock() const {
  if (_mutex) xSemaphoreGive(_mutex);
}

void EventLog::log(const char *fmt, ...) {
  char text[kTextLen];
  va_list args;
  va_start(args, fmt);
  vsnprintf(text, sizeof(text), fmt, args);
  va_end(args);

  const uint32_t now = millis();

  lock();
  Entry &e = _entries[(_nextSeq - 1) % kCapacity];
  e.seq = _nextSeq++;
  e.uptimeMs = now;
  strlcpy(e.text, text, sizeof(e.text));
  unlock();

  Serial.printf("[%8lu] %s\r\n", (unsigned long)now, text);
}

uint32_t EventLog::latestSeq() const {
  lock();
  uint32_t seq = _nextSeq - 1;
  unlock();
  return seq;
}

void EventLog::forEachSince(uint32_t since, const std::function<void(const Entry &)> &fn) const {
  lock();
  const uint32_t newest = _nextSeq - 1;
  const uint32_t oldest = (newest > kCapacity) ? newest - kCapacity + 1 : 1;
  uint32_t first = (since + 1 > oldest) ? since + 1 : oldest;
  for (uint32_t seq = first; seq <= newest; ++seq) {
    fn(_entries[(seq - 1) % kCapacity]);
  }
  unlock();
}
