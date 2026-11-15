#pragma once

#include "GarageDoorModel.h"

class DoorPositionStore {
public:
  /**
   * @brief Loads the saved door position from persistent storage.
   * @return True if storage was opened and read; otherwise, false.
   */
  bool load();

  /**
   * @brief Removes the saved door position from persistent storage.
   * @return True if no record remains; otherwise, false.
   */
  bool clear();

  /**
   * @brief Saves a valid door position to persistent storage.
   * @param[in] position Position record to save.
   * @return True if the record is valid and saved; otherwise, false.
   */
  bool save(const GarageDoorModel::Position &position);

  /**
   * @brief Checks whether a saved position record is present.
   * @return True if a record is present; otherwise, false.
   */
  bool present() const { return _present; }

  /**
   * @brief Gets the last position record loaded or saved by this store.
   * @return A constant reference to the cached position record.
   */
  const GarageDoorModel::Position &record() const { return _record; }

private:
  GarageDoorModel::Position _record = {};
  bool _present = false;
};
