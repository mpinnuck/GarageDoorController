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
