#pragma once

class DoorControlGate {
public:
  using Save = bool (*)(bool enabled);

  /**
   * @brief Creates a control gate with a persistence callback.
   * @param[in] save Callback that saves the requested enabled state.
   */
  explicit DoorControlGate(Save save) : _save(save) {}

  /**
   * @brief Restores the enabled state without invoking the persistence callback.
   * @param[in] enabled State to restore.
   */
  void restore(bool enabled) { _enabled = enabled; }

  /**
   * @brief Checks whether opener control is enabled.
   * @return True if control is enabled; otherwise, false.
   */
  bool enabled() const { return _enabled; }

  /**
   * @brief Saves and applies a requested control state.
   * @param[in] enabled True to enable control, or false to disable it.
   * @return True if the state was saved and applied; otherwise, false.
   */
  bool setEnabled(bool enabled) {
    if (!enabled) _enabled = false;
    if (!_save(enabled)) {
      _enabled = false;
      return false;
    }
    _enabled = enabled;
    return true;
  }

private:
  Save _save;
  bool _enabled = false;
};
