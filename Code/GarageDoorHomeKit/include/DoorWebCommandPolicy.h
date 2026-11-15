#pragma once

#include "GarageDoorModel.h"

class DoorWebCommandPolicy {
public:
  enum class Command { Open, Close, Stop };
  enum class Action { Press, NoOp, Reject, Failed };
  struct Result {
    Action action;
    const char *reason;
  };

  static bool shouldPress(const GarageDoorModel &door, Command command) {
    using State = GarageDoorModel::State;
    const State state = door.state();
    switch (command) {
      case Command::Open:
        return state == State::Closed ||
               (state == State::Stopped && door.directionConfirmed() &&
                door.nextDirection() == State::Opening);
      case Command::Close:
        return door.directionConfirmed() &&
               (state == State::Open ||
                (state == State::Stopped && door.nextDirection() == State::Closing));
      case Command::Stop:
        return !door.pending() && (state == State::Opening || state == State::Closing);
    }
    return false;
  }

  static Result evaluate(const GarageDoorModel &door, Command command) {
    using State = GarageDoorModel::State;
    if (shouldPress(door, command)) return {Action::Press, nullptr};
    if (command == Command::Stop && door.pending())
      return {Action::Reject, "opener start or stop awaiting confirmation"};

    const State state = door.state();
    if (!door.moving() && !door.motorRunning() && !door.pending()) {
      if (command == Command::Open && state == State::Open)
        return {Action::NoOp, "already open"};
      if (command == Command::Close && state == State::Closed)
        return {Action::NoOp, "already closed"};
      if (command == Command::Stop)
        return {Action::NoOp, "already stopped"};
    }

    if (door.startupDirectionUnknown())
      return {Action::Reject, "door position unknown after restart"};
    if (door.moving() || door.motorRunning() || door.pending())
      return {Action::Reject, "door movement in progress or awaiting confirmation"};
    if (!door.directionConfirmed())
      return {Action::Reject, "door direction unconfirmed"};
    return {Action::Reject, "next press would move in opposite direction"};
  }
};
