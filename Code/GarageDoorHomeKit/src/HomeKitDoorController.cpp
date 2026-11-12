// HomeKitDoorController.cpp

#include "HomeKitDoorController.h"
#include "EventLog.h"

static_assert(static_cast<int>(HomeKitDoorPolicy::Current::Open) == Characteristic::CurrentDoorState::OPEN &&
              static_cast<int>(HomeKitDoorPolicy::Current::Closed) == Characteristic::CurrentDoorState::CLOSED &&
              static_cast<int>(HomeKitDoorPolicy::Current::Opening) == Characteristic::CurrentDoorState::OPENING &&
              static_cast<int>(HomeKitDoorPolicy::Current::Closing) == Characteristic::CurrentDoorState::CLOSING &&
              static_cast<int>(HomeKitDoorPolicy::Current::Stopped) == Characteristic::CurrentDoorState::STOPPED &&
              static_cast<int>(HomeKitDoorPolicy::Target::Open) == Characteristic::TargetDoorState::OPEN &&
              static_cast<int>(HomeKitDoorPolicy::Target::Closed) == Characteristic::TargetDoorState::CLOSED,
              "HomeKit door state values must match HomeSpan");

// HomeKit ContactSensorState: 0 = contact detected (closed), 1 = not detected (open)
static uint8_t toHomeKit(bool closed) { return closed ? 0 : 1; }

HomeKitDoorController::HomeKitDoorController(bool closed) : Service::ContactSensor() {
  _state = new Characteristic::ContactSensorState(toHomeKit(closed));
}

void HomeKitDoorController::setClosed(bool closed) {
  const uint8_t v = toHomeKit(closed);
  if (_state->getVal() != v) _state->setVal(v);
}

HomeKitMotionSensor::HomeKitMotionSensor(bool moving) : Service::MotionSensor() {
  new Characteristic::Name("Door Moving");
  _motion = new Characteristic::MotionDetected(moving);
}

void HomeKitMotionSensor::setMoving(bool moving) {
  if (_motion->getVal() != moving) _motion->setVal(moving);
}

HomeKitGarageDoor::HomeKitGarageDoor(GarageDoorModel &model, const char *(*unavailable)(),
                                   bool (*press)(bool))
    : Service::GarageDoorOpener(), _model(model), _unavailable(unavailable), _press(press) {
  const uint8_t current = static_cast<uint8_t>(HomeKitDoorPolicy::current(model));
  _lastSettledTarget = model.state() == GarageDoorModel::State::Closed
                           ? static_cast<uint8_t>(HomeKitDoorPolicy::Target::Closed)
                           : static_cast<uint8_t>(HomeKitDoorPolicy::Target::Open);
  _current = new Characteristic::CurrentDoorState(current);
  _target = new Characteristic::TargetDoorState(_lastSettledTarget);
  _obstruction = new Characteristic::ObstructionDetected(model.obstruction());
}

boolean HomeKitGarageDoor::update() {
  const int requested = _target->getNewVal();
  if (requested != static_cast<int>(HomeKitDoorPolicy::Target::Open) &&
      requested != static_cast<int>(HomeKitDoorPolicy::Target::Closed)) {
    Log.log("HomeKit door target rejected: invalid value");
    return false;
  }
  const char *targetName = requested == static_cast<int>(HomeKitDoorPolicy::Target::Open)
                               ? "Open" : "Close";
  const char *reason = _unavailable();
  if (reason) {
    Log.log("HomeKit %s rejected: %s", targetName, reason);
    return false;
  }
  const auto action = HomeKitDoorPolicy::action(
      _model, static_cast<HomeKitDoorPolicy::Target>(requested), true, &reason);
  if (action == HomeKitDoorPolicy::Action::Reject) {
    Log.log("HomeKit %s rejected: %s", targetName, reason);
    return false;
  }
  if (action == HomeKitDoorPolicy::Action::NoOp) return true;
  if (_press(action == HomeKitDoorPolicy::Action::PressAssumingStartupClose)) return true;
  Log.log("HomeKit %s rejected: opener press failed (see previous error)", targetName);
  return false;
}

void HomeKitGarageDoor::sync(GarageDoorModel::Event event) {
  const uint8_t current = static_cast<uint8_t>(HomeKitDoorPolicy::current(_model));
  if (_current->getVal() != current) _current->setVal(current);
  if (_obstruction->getVal() != _model.obstruction())
    _obstruction->setVal(_model.obstruction());

  uint8_t target = _target->getVal();
  if (event == GarageDoorModel::Event::StartFailed) {
    target = _model.state() == GarageDoorModel::State::Closed
                 ? static_cast<uint8_t>(HomeKitDoorPolicy::Target::Closed)
                 : _lastSettledTarget;
  } else if (!_model.pending()) {
    const auto movement = _model.movementState();
    if (_model.motorRunning() && _model.directionConfirmed() &&
        movement == GarageDoorModel::State::Opening)
      target = static_cast<uint8_t>(HomeKitDoorPolicy::Target::Open);
    else if (_model.motorRunning() && _model.directionConfirmed() &&
             movement == GarageDoorModel::State::Closing)
      target = static_cast<uint8_t>(HomeKitDoorPolicy::Target::Closed);
    else if (_model.state() == GarageDoorModel::State::Closed)
      target = static_cast<uint8_t>(HomeKitDoorPolicy::Target::Closed);
    else if (_model.state() == GarageDoorModel::State::Open)
      target = static_cast<uint8_t>(HomeKitDoorPolicy::Target::Open);
  }
  if (_target->getVal() != target) _target->setVal(target);
  if (!_model.pending()) _lastSettledTarget = target;
}
