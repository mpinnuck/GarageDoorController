#include "GarageDoorModel.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

using State = GarageDoorModel::State;
using Event = GarageDoorModel::Event;

static void testMotionInput() {
  MotionInput input;
  assert(!input.update(true, 100));
  assert(!input.update(true, 149));
  assert(!input.update(false, 149));
  assert(!input.update(true, 200));
  assert(input.update(true, 250));
  assert(input.update(false, 300));
  assert(input.update(false, 1799));
  assert(input.update(true, 1799));
  assert(input.update(false, 1900));
  assert(!input.update(false, 3400));

  MotionInput noise;
  assert(!noise.update(true, 0));
  assert(noise.update(true, 50));
  assert(noise.update(false, 100));
  assert(noise.update(true, 1580));
  assert(noise.update(false, 1599));
  assert(!noise.update(false, 1600));

  MotionInput sparse;
  assert(!sparse.update(true, 100));
  assert(sparse.update(false, 150));
  assert(!sparse.update(false, 1650));

  MotionInput wrap;
  assert(!wrap.update(true, UINT32_MAX - 25));
  assert(wrap.update(true, 24));
  assert(wrap.update(false, UINT32_MAX - 500));
  assert(!wrap.update(false, 999));
}

static void testStartFailureAndExternalMovement() {
  GarageDoorModel model(true);
  model.begin(true, 0);
  assert(model.press(100));
  assert(!model.press(200));
  assert(model.update(true, false, 3099) == Event::None);
  assert(model.update(true, false, 3100) == Event::StartFailed);
  assert(model.state() == State::Closed);
  assert(model.press(4000));
  model.update(true, true, 4100);
  assert(!model.pending());
  model.update(false, true, 5000);
  assert(model.state() == State::Opening);
  model.update(false, false, 21000);
  assert(model.state() == State::Open);
  model.update(false, true, 22000);
  assert(model.state() == State::Closing);
  model.update(true, true, 34000);
  assert(model.state() == State::Closed);
  model.update(true, false, 35500);
  assert(model.state() == State::Closed);
}

static void testStopReverseAndObstruction() {
  GarageDoorModel model(true);
  model.begin(false, 0);
  model.update(false, true, 100);
  assert(model.state() == State::MovingUnknown);
  model.update(false, false, 2000);
  assert(model.state() == State::Stopped);

  GarageDoorModel closing(true);
  closing.begin(true, 0);
  closing.update(true, true, 100);
  closing.update(false, true, 200);
  closing.update(false, false, 16600);
  assert(closing.state() == State::Open);
  closing.update(false, true, 17000);
  assert(closing.state() == State::Closing);
  assert(closing.press(20000));
  closing.update(false, false, 21600);
  assert(closing.state() == State::Stopped);
  assert(!closing.obstruction());
  closing.update(false, true, 22000);
  assert(closing.state() == State::Opening);
  assert(closing.press(23000));
  assert(closing.update(false, true, 26000) == Event::Reversed);
  assert(closing.state() == State::Closing);
  closing.update(false, false, 41000);
  assert(closing.state() == State::Open);
  assert(closing.obstruction());
}

static void testTimeoutAndFallback() {
  GarageDoorModel model(true);
  model.begin(true, 0);
  model.update(true, true, 100);
  model.update(false, true, 200);
  assert(model.update(false, true, 30099) == Event::None);
  assert(model.update(false, true, 30100) == Event::SafetyTimeout);
  assert(model.state() == State::Stopped);
  assert(!model.press(30101));
  model.update(false, false, 31000);
  assert(model.press(31001));

  GarageDoorModel fallback(false);
  fallback.begin(true, 0);
  assert(fallback.press(100));
  fallback.update(false, false, 500);
  fallback.update(false, false, 15099);
  assert(fallback.state() == State::Opening);
  fallback.update(false, false, 15100);
  assert(fallback.state() == State::Open);
  assert(fallback.press(16000));
  fallback.update(false, false, 29499);
  assert(fallback.state() == State::Closing);
  fallback.update(true, false, 29500);
  assert(fallback.state() == State::Closed);

  GarageDoorModel external(false);
  external.begin(true, 0);
  external.update(false, false, 100);
  assert(external.state() == State::Opening);
  assert(external.press(1000));
  assert(external.state() == State::Stopped);
  assert(external.press(2000));
  assert(external.state() == State::Closing);
}

static void testReedPriorityAndFailedStart() {
  GarageDoorModel model(true);
  model.begin(true, 0);
  model.update(true, true, 100);
  assert(model.state() == State::Closed);
  assert(!model.canPress());
  model.update(true, false, 2000);
  assert(model.press(3000));
  model.update(false, false, 3500);
  assert(model.update(false, false, 6000) == Event::StartFailed);
  assert(model.state() == State::Stopped);
}

int main() {
  testMotionInput();
  testStartFailureAndExternalMovement();
  testStopReverseAndObstruction();
  testTimeoutAndFallback();
  testReedPriorityAndFailedStart();
  std::puts("Garage door model tests passed");
}
