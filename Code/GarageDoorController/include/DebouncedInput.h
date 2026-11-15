// DebouncedInput.h
// A digital input that only reports a change after it has been stable
// for the debounce time. Used for the door reed switch.

#pragma once

#include <Arduino.h>

class DebouncedInput {
public:
  /**
   * @brief Creates a debounced digital input.
   * @param[in] pin GPIO pin to read.
   * @param[in] activeLow True if a LOW reading represents the active state.
   * @param[in] debounceMs Required stable time before accepting a state change.
   */
  DebouncedInput(uint8_t pin, bool activeLow, uint32_t debounceMs);

  /**
   * @brief Configures the pin and initializes the stable input state.
   */
  void begin();

  /**
   * @brief Samples the input and updates its debounced state.
   * @return True if the stable state changed; otherwise, false.
   */
  bool update();

  /**
   * @brief Gets the current debounced input state.
   * @return True if the input is active; otherwise, false.
   */
  bool active() const { return _active; }

private:
  /**
   * @brief Reads the pin and converts its electrical level to an active state.
   * @return True if the raw input represents the active state.
   */
  bool readRaw() const;

  const uint8_t _pin;
  const bool _activeLow;
  const uint32_t _debounceMs;
  bool _active = false;
  bool _lastRaw = false;
  uint32_t _lastRawChangeMs = 0;
};
