// HomeKitDoorSensor.cpp

#include "HomeKitDoorSensor.h"

// HomeKit ContactSensorState: 0 = contact detected (closed), 1 = not detected (open)
static uint8_t toHomeKit(bool closed) { return closed ? 0 : 1; }

HomeKitDoorSensor::HomeKitDoorSensor(bool closed) : Service::ContactSensor() {
  _state = new Characteristic::ContactSensorState(toHomeKit(closed));
}

void HomeKitDoorSensor::setClosed(bool closed) {
  const uint8_t v = toHomeKit(closed);
  if (_state->getVal() != v) _state->setVal(v);
}
