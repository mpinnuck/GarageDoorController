#include "DoorPositionStore.h"

#include <Preferences.h>
#include <string.h>

bool DoorPositionStore::load() {
  Preferences prefs;
  if (!prefs.begin("door-model", false)) return false;
  _present = prefs.isKey("position");
  _record = {};
  if (_present && (prefs.getBytesLength("position") != sizeof(_record) ||
      prefs.getBytes("position", &_record, sizeof(_record)) != sizeof(_record)))
    _record = {};
  prefs.end();
  return true;
}

bool DoorPositionStore::clear() {
  if (!_present) return true;
  Preferences prefs;
  if (!prefs.begin("door-model", false)) return false;
  const bool removed = prefs.remove("position");
  prefs.end();
  if (removed) _present = false;
  return removed;
}

bool DoorPositionStore::save(const GarageDoorModel::Position &position) {
  if (!GarageDoorModel::validPosition(position)) return false;
  if (_present && memcmp(&_record, &position, sizeof(position)) == 0) return true;
  Preferences prefs;
  if (!prefs.begin("door-model", false)) return false;
  const bool saved = prefs.putBytes("position", &position, sizeof(position)) == sizeof(position);
  prefs.end();
  if (saved) {
    _record = position;
    _present = true;
  }
  return saved;
}
