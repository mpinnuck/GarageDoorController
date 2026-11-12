#include "HomeKitDoorController.h"
#include "EventLog.h"

#include <cassert>
#include <cstdio>
#include <cstring>

EventLog Log;
static GarageDoorModel *model;
static uint32_t now;
static int presses;
static const char *unavailableReason = nullptr;

static const char *unavailable() { return unavailableReason; }
static bool press(bool assumeStartupClose) {
  ++presses;
  return model->press(now, assumeStartupClose);
}

static bool writeTarget(HomeKitGarageDoor &service, int value) {
  auto *target = Characteristic::TargetDoorState::latest();
  target->newValue = value;
  if (!service.update()) {
    target->newValue = target->value;
    return false;
  }
  target->value = target->newValue;
  return true;
}

static int target() { return Characteristic::TargetDoorState::latest()->getVal(); }
static int current() { return Characteristic::CurrentDoorState::latest()->getVal(); }
static bool obstructed() { return Characteristic::ObstructionDetected::latest()->getVal(); }

static void testFailedStart() {
  using Event = GarageDoorModel::Event;
  GarageDoorModel door(true);
  model = &door;
  door.begin(true, 0);
  HomeKitGarageDoor service(door, unavailable, press);
  now = 100;
  assert(writeTarget(service, 0));
  assert(presses == 1 && target() == 0);
  service.sync(Event::None);
  assert(target() == 0 && current() == 1);
  assert(!writeTarget(service, 1));
  assert(std::strcmp(Log.last, "HomeKit Close rejected: opener start or stop awaiting confirmation") == 0);
  assert(target() == 0 && presses == 1);
  service.sync(door.update(true, false, 3100));
  assert(target() == 1 && current() == 1);
}

static void testExternalAndPartialMovement() {
  GarageDoorModel door(true);
  model = &door;
  door.begin(true, 0);
  HomeKitGarageDoor service(door, unavailable, press);
  service.sync(door.update(true, true, 100));
  assert(target() == 0 && current() == 1);
  service.sync(door.update(false, true, 200));
  assert(current() == 2);
  assert(!writeTarget(service, 1));
  assert(std::strcmp(Log.last, "HomeKit Close rejected: door moving in opposite direction") == 0);
  service.sync(door.update(false, false, 16600));
  assert(target() == 0 && current() == 0);
  service.sync(door.update(false, true, 17000));
  assert(target() == 1 && current() == 3);
  service.sync(door.update(false, false, 19000));
  assert(current() == 4 && obstructed());
  now = 20000;
  const int before = presses;
  assert(!writeTarget(service, 1));
  assert(std::strcmp(Log.last, "HomeKit Close rejected: door direction unconfirmed") == 0);
  assert(presses == before && target() == 1);
  assert(!writeTarget(service, 0));
  assert(presses == before && target() == 1);
  service.sync(door.update(true, false, 20100));
  assert(current() == 1 && !obstructed());
  now = 21000;
  assert(writeTarget(service, 0));
  assert(presses == before + 1);
}

static void testStartupAndDisabled() {
  GarageDoorModel door(true);
  model = &door;
  door.begin(false, 0);
  HomeKitGarageDoor service(door, unavailable, press);
  unavailableReason = "door control disabled";
  assert(!writeTarget(service, 1));
  assert(std::strcmp(Log.last, "HomeKit Close rejected: door control disabled") == 0);
  unavailableReason = "door position storage fault";
  assert(!writeTarget(service, 1));
  assert(std::strcmp(Log.last, "HomeKit Close rejected: door position storage fault") == 0);
  unavailableReason = nullptr;
  assert(!writeTarget(service, 0));
  assert(std::strcmp(Log.last, "HomeKit Open rejected: door direction unknown after startup") == 0);
  now = 100;
  assert(writeTarget(service, 1));
  service.sync(door.update(false, true, 200));
  assert(target() == 1 && current() == 4);
  assert(!writeTarget(service, 0));
  assert(std::strcmp(Log.last, "HomeKit Open rejected: movement direction unconfirmed") == 0);
  service.sync(door.update(true, true, 300));
  assert(target() == 1 && current() == 1);
}

static void testOtherRejections() {
  GarageDoorModel noLamp(false);
  model = &noLamp;
  noLamp.begin(true, 0);
  HomeKitGarageDoor service(noLamp, unavailable, press);
  assert(!writeTarget(service, 0));
  assert(std::strcmp(Log.last, "HomeKit Open rejected: motion input not fitted") == 0);
  assert(!writeTarget(service, 2));
  assert(std::strcmp(Log.last, "HomeKit door target rejected: invalid value") == 0);

  GarageDoorModel stopped(true);
  model = &stopped;
  stopped.begin(true, 0);
  stopped.update(true, true, 100);
  stopped.update(false, true, 200);
  assert(stopped.press(1000));
  stopped.update(false, false, 3000);
  HomeKitGarageDoor stoppedService(stopped, unavailable, press);
  assert(!writeTarget(stoppedService, 0));
  assert(std::strcmp(Log.last, "HomeKit Open rejected: next press would move in opposite direction") == 0);
}

int main() {
  testFailedStart();
  testExternalAndPartialMovement();
  testStartupAndDisabled();
  testOtherRejections();
  std::puts("HomeKit door tests passed");
}
