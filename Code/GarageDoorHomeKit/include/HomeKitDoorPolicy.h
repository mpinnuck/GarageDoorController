#pragma once

#include "GarageDoorModel.h"

class HomeKitDoorPolicy {
public:
  enum class Current : uint8_t { Open, Closed, Opening, Closing, Stopped };
  enum class Target : uint8_t { Open, Closed };
  enum class Action { Reject, NoOp, Press, PressAssumingStartupClose };

  static Current current(const GarageDoorModel &model) {
    switch (model.state()) {
      case GarageDoorModel::State::Closed: return Current::Closed;
      case GarageDoorModel::State::Open: return Current::Open;
      case GarageDoorModel::State::Opening:
        return model.motorRunning() && model.directionConfirmed()
                   ? Current::Opening : Current::Stopped;
      case GarageDoorModel::State::Closing:
        return model.motorRunning() && model.directionConfirmed()
                   ? Current::Closing : Current::Stopped;
      case GarageDoorModel::State::Stopped:
      case GarageDoorModel::State::MovingUnknown: return Current::Stopped;
    }
    return Current::Stopped;
  }

  static Action action(const GarageDoorModel &model, Target target, bool ready,
                       const char **reason = nullptr) {
    using State = GarageDoorModel::State;
    if (!ready) return reject(reason, "controller unavailable");
    if (model.pending()) return reject(reason, "opener start or stop awaiting confirmation");

    const State movement = model.movementState();
    if (model.moving()) {
      if (!model.flashFitted()) return reject(reason, "motion input not fitted");
      if (!model.motorRunning()) return reject(reason, "motor start not confirmed");
      if (!model.directionConfirmed()) return reject(reason, "movement direction unconfirmed");
      if ((movement == State::Opening && target == Target::Open) ||
          (movement == State::Closing && target == Target::Closed)) return Action::NoOp;
      return reject(reason, "door moving in opposite direction");
    }

    const State state = model.state();
    if ((state == State::Closed && target == Target::Closed) ||
        (state == State::Open && target == Target::Open)) return Action::NoOp;
    if (!model.flashFitted()) return reject(reason, "motion input not fitted");
    if (!model.canPress()) return reject(reason, "opener waiting for motor to stop");
    if (state == State::Closed && target == Target::Open) return Action::Press;
    if (state == State::Open && target == Target::Closed) return Action::Press;
    if (state == State::Stopped && target == Target::Closed &&
        model.startupDirectionUnknown()) return Action::PressAssumingStartupClose;
    if (state == State::Stopped && model.directionConfirmed() &&
        ((model.nextDirection() == State::Opening && target == Target::Open) ||
         (model.nextDirection() == State::Closing && target == Target::Closed)))
      return Action::Press;
    if (model.startupDirectionUnknown())
      return reject(reason, "door direction unknown after startup");
    if (!model.directionConfirmed()) return reject(reason, "door direction unconfirmed");
    return reject(reason, "next press would move in opposite direction");
  }

private:
  static Action reject(const char **reason, const char *message) {
    if (reason) *reason = message;
    return Action::Reject;
  }
};
