// HomeKitDoorController.cpp

#include "HomeKitDoorController.h"

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

HomeKitOpenerSwitch::HomeKitOpenerSwitch(DoorPressCallback press)
    : Service::Switch(), _press(press) {
  new Characteristic::Name("Open Stop Close");
  _on = new Characteristic::On(false);
}

boolean HomeKitOpenerSwitch::update() {
  if (!_on->getNewVal()) return true;
  if (!_press()) return false;
  _reset = true;
  return true;
}

void HomeKitOpenerSwitch::loop() {
  if (_reset && _on->getVal()) {
    _on->setVal(false);
    _reset = false;
  }
}
