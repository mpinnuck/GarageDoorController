#pragma once

#include "GarageDoorModel.h"

class DoorWebCommandPolicy {
public:
  enum class Command { Open, Close, Stop };
  enum class Result { Ignored, Pressed, Failed };

  static bool shouldPress(const GarageDoorModel &door, Command command) {
    using State = GarageDoorModel::State;
    const State state = door.state();
    switch (command) {
      case Command::Open:
        return state == State::Closed ||
               (state == State::Stopped && door.nextDirection() == State::Opening);
      case Command::Close:
        return state == State::Open ||
               (state == State::Stopped && door.nextDirection() == State::Closing);
      case Command::Stop:
        return state == State::Opening || state == State::Closing;
    }
    return false;
  }
};
