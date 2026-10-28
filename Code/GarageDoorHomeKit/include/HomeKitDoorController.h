// HomeKitDoorController.h
// HomeKit Contact Sensor service for the garage door.
// Apple Home shows it as Open / Closed, and Siri answers
// "Is the garage door open?". Call setClosed() whenever the door changes.

#pragma once

#include <HomeSpan.h>

class HomeKitDoorController : public Service::ContactSensor {
public:
  explicit HomeKitDoorController(bool closed);

  void setClosed(bool closed);

private:
  Characteristic::ContactSensorState *_state;
};
