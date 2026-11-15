#pragma once

class DoorControlGate {
public:
  using Save = bool (*)(bool enabled);

  explicit DoorControlGate(Save save) : _save(save) {}
  void restore(bool enabled) { _enabled = enabled; }
  bool enabled() const { return _enabled; }

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
