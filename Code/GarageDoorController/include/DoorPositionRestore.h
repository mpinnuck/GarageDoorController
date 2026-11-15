#pragma once

#include "GarageDoorModel.h"

class DoorPositionRestore {
public:
  enum class Reset {
    Software, Panic, InterruptWatchdog, TaskWatchdog, Watchdog, Usb, PowerOn, Brownout, Other
  };
  enum class Decision { Restore, ResetNotEligible, ReedClosed, NoRecord, InvalidRecord };

  static Decision decide(Reset reset, bool closed, bool present,
                         const GarageDoorModel::Position &position) {
    if (closed) return Decision::ReedClosed;
    if (reset == Reset::PowerOn || reset == Reset::Brownout || reset == Reset::Other)
      return Decision::ResetNotEligible;
    if (!present) return Decision::NoRecord;
    if (!GarageDoorModel::validPosition(position)) return Decision::InvalidRecord;
    return Decision::Restore;
  }
};
