// DebouncedInput.h
// A digital input that only reports a change after it has been stable
// for the debounce time. Used for the door reed switch.

#pragma once

#include <Arduino.h>

class DebouncedInput {
public:
  // activeLow: true if the input reads LOW when "active".
  DebouncedInput(uint8_t pin, bool activeLow, uint32_t debounceMs);

  void begin();          // configures the pin and takes the initial reading
  bool update();         // call often; returns true when the stable state changes
  bool active() const { return _active; }

private:
  bool readRaw() const;

  const uint8_t _pin;
  const bool _activeLow;
  const uint32_t _debounceMs;
  bool _active = false;
  bool _lastRaw = false;
  uint32_t _lastRawChangeMs = 0;
};
