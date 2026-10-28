// DebouncedInput.cpp

#include "DebouncedInput.h"

DebouncedInput::DebouncedInput(uint8_t pin, bool activeLow, uint32_t debounceMs)
    : _pin(pin), _activeLow(activeLow), _debounceMs(debounceMs) {}

void DebouncedInput::begin() {
  pinMode(_pin, INPUT);   // external pull-up fitted on the PCB
  _active = _lastRaw = readRaw();
  _lastRawChangeMs = millis();
}

bool DebouncedInput::readRaw() const {
  return (digitalRead(_pin) == LOW) == _activeLow;
}

bool DebouncedInput::update() {
  const bool raw = readRaw();
  const uint32_t now = millis();
  if (raw != _lastRaw) {
    _lastRaw = raw;
    _lastRawChangeMs = now;
  }
  if (raw != _active && now - _lastRawChangeMs >= _debounceMs) {
    _active = raw;
    return true;
  }
  return false;
}
