#pragma once

#include "GarageDoorModel.h"

class DoorPositionStore {
public:
  bool load();
  bool clear();
  bool save(const GarageDoorModel::Position &position);
  bool present() const { return _present; }
  const GarageDoorModel::Position &record() const { return _record; }

private:
  GarageDoorModel::Position _record = {};
  bool _present = false;
};
