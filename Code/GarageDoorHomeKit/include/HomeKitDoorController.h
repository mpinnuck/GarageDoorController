// HomeKitDoorController.h
// HomeKit Contact Sensor service for the garage door.
// Apple Home shows it as Open / Closed, and Siri answers
// "Is the garage door open?". Call setClosed() whenever the door changes.

#pragma once

#include <HomeSpan.h>

using DoorPressCallback = bool (*)();

class HomeKitDoorController : public Service::ContactSensor {
public:
  explicit HomeKitDoorController(bool closed);

  void setClosed(bool closed);

private:
  Characteristic::ContactSensorState *_state;
};

class HomeKitMotionSensor : public Service::MotionSensor {
public:
  explicit HomeKitMotionSensor(bool moving);
  void setMoving(bool moving);

private:
  Characteristic::MotionDetected *_motion;
};

class HomeKitOpenerSwitch : public Service::Switch {
public:
  explicit HomeKitOpenerSwitch(DoorPressCallback press);
  boolean update() override;
  void loop() override;

private:
  DoorPressCallback _press;
  Characteristic::On *_on;
  bool _reset = false;
};
