// HomeKitDoorController.h
// HomeKit Contact Sensor service for the garage door.
// Apple Home shows it as Open / Closed, and Siri answers
// "Is the garage door open?". Call setClosed() whenever the door changes.

#pragma once

#include <HomeSpan.h>

#include "GarageDoorModel.h"
#include "HomeKitDoorPolicy.h"

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

class HomeKitGarageDoor : public Service::GarageDoorOpener {
public:
  HomeKitGarageDoor(GarageDoorModel &model, const char *(*unavailable)(), bool (*press)(bool));
  boolean update() override;
  void sync(GarageDoorModel::Event event);

private:
  GarageDoorModel &_model;
  const char *(*_unavailable)();
  bool (*_press)(bool);
  Characteristic::CurrentDoorState *_current;
  Characteristic::TargetDoorState *_target;
  Characteristic::ObstructionDetected *_obstruction;
  uint8_t _lastSettledTarget;
};
