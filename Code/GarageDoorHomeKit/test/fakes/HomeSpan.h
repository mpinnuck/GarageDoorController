#pragma once

#include <cstdint>

using boolean = bool;

namespace Service {
struct ContactSensor {};
struct MotionSensor {};
struct GarageDoorOpener {
  virtual boolean update() { return true; }
};
}

namespace Characteristic {
template <typename Tag>
struct Value {
  explicit Value(int value) : value(value), newValue(value) { latest() = this; }
  int getVal() const { return value; }
  int getNewVal() const { return newValue; }
  void setVal(int v) { value = newValue = v; }
  static Value *&latest() { static Value *p = nullptr; return p; }
  int value;
  int newValue;
};
struct ContactSensorState : Value<ContactSensorState> { using Value::Value; };
struct MotionDetected : Value<MotionDetected> { using Value::Value; };
struct CurrentDoorState : Value<CurrentDoorState> {
  enum { OPEN, CLOSED, OPENING, CLOSING, STOPPED };
  using Value::Value;
};
struct TargetDoorState : Value<TargetDoorState> {
  enum { OPEN, CLOSED };
  using Value::Value;
};
struct ObstructionDetected : Value<ObstructionDetected> { using Value::Value; };
struct Name { explicit Name(const char *) {} };
}
