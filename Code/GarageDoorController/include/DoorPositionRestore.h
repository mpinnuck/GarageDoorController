#pragma once

#include "GarageDoorModel.h"

class DoorPositionRestore {
public:
  enum class Reset {
    Software, Panic, InterruptWatchdog, TaskWatchdog, Watchdog, Usb, PowerOn, Brownout, Other
  };
  enum class Decision { Restore, ResetNotEligible, ReedClosed, NoRecord, InvalidRecord };

  /**
   * @brief Determines whether a saved door position may be restored.
   * @param[in] reset Cause of the most recent device reset.
   * @param[in] closed True if the closed-position reed switch is active.
   * @param[in] present True if a saved position record exists.
   * @param[in] position Saved position record to validate.
   * @return The reason to restore or reject the saved position.
   */
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
