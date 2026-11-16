#include "GarageDoorModel.h"
#include "DoorWebCommandPolicy.h"
#include "DoorPositionRestore.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

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
  assert(closing.directionConfirmed());
  closing.update(false, true, 22000);
  assert(closing.state() == State::Opening);
  assert(closing.press(23000));
  assert(closing.update(false, true, 26000) == Event::Reversed);
  assert(closing.state() == State::Closing);
  closing.update(false, false, 41000);
  assert(closing.state() == State::Stopped);
  assert(closing.obstruction());
}

static void testOpenCompletionMargin() {
  GarageDoorModel shortMovement(true);
  shortMovement.begin(true, 0);
  shortMovement.update(true, true, 100);
  shortMovement.update(false, true, 200);
  shortMovement.update(false, false, 14599);
  assert(shortMovement.state() == State::Stopped);
  assert(!shortMovement.directionConfirmed());

  GarageDoorModel fullMovement(true);
  fullMovement.begin(true, 0);
  fullMovement.update(true, true, 100);
  fullMovement.update(false, true, 200);
  fullMovement.update(false, false, 14600);
  assert(fullMovement.state() == State::Open);

  GarageDoorModel commandedStop(true);
  commandedStop.begin(true, 0);
  assert(commandedStop.press(100));
  commandedStop.update(true, true, 200);
  commandedStop.update(false, true, 300);
  assert(commandedStop.press(13000));
  commandedStop.update(false, false, 14600);
  assert(commandedStop.state() == State::Stopped);
  assert(commandedStop.directionConfirmed());
}

static void testMotorStopBeforeMovementStart() {
  for (const uint32_t modelStart : {2001u, 3600u}) {
    MotionInput input;
    GarageDoorModel model(true);
    model.begin(true, 0);
    assert(!input.update(true, 100));
    const bool running = input.update(false, 2000);
    assert(running);
    model.update(true, running, modelStart);
    model.update(false, running, modelStart);
    assert(model.state() == State::Opening);
    assert(model.directionConfirmed());

    const uint32_t stopTime = modelStart == 2001 ? 3500 : 3601;
    assert(!input.update(false, stopTime));
    model.update(false, false, stopTime);
    assert(model.state() == State::Stopped);
    assert(!model.directionConfirmed());
  }

  for (const uint32_t elapsed : {1499u, 1500u, 1501u}) {
    GarageDoorModel model(true);
    const uint32_t start = UINT32_MAX - 1000;
    model.begin(true, start);
    model.update(true, true, start);
    model.update(false, true, start);
    model.update(false, false, start + elapsed);
    assert(model.state() == State::Stopped);
    assert(!model.directionConfirmed());
  }

  GarageDoorModel fullMovement(true);
  const uint32_t start = UINT32_MAX - 1000;
  fullMovement.begin(true, start);
  fullMovement.update(true, true, start);
  fullMovement.update(false, true, start);
  fullMovement.update(false, false, start + GarageDoorModel::kOpenCompletionMs + 1500);
  assert(fullMovement.state() == State::Open);
  assert(fullMovement.directionConfirmed());
}

static void testDoorWebCommands() {
  using Command = DoorWebCommandPolicy::Command;
  using Action = DoorWebCommandPolicy::Action;

  GarageDoorModel closed(true);
  closed.begin(true, 0);
  assert(DoorWebCommandPolicy::shouldPress(closed, Command::Open));
  assert(!DoorWebCommandPolicy::shouldPress(closed, Command::Close));
  assert(!DoorWebCommandPolicy::shouldPress(closed, Command::Stop));

  GarageDoorModel powerOnOpen(true);
  powerOnOpen.begin(false, 0);
  assert(powerOnOpen.state() == State::Open);
  assert(powerOnOpen.startupDirectionUnknown());
  assert(!powerOnOpen.directionConfirmed());
  auto result = DoorWebCommandPolicy::evaluate(powerOnOpen, Command::Open);
  assert(result.action == Action::NoOp);
  assert(std::strcmp(result.reason, "already open") == 0);
  assert(DoorWebCommandPolicy::shouldPress(powerOnOpen, Command::Close));
  assert(powerOnOpen.press(100));
  assert(powerOnOpen.state() == State::Closing);
  assert(powerOnOpen.pending());
  assert(powerOnOpen.update(false, true, 200) == Event::None);
  assert(powerOnOpen.state() == State::Closing);
  assert(powerOnOpen.directionConfirmed());
  powerOnOpen.update(true, true, 300);
  assert(powerOnOpen.state() == State::Closed);

  GarageDoorModel failedClose(true);
  failedClose.begin(false, 0);
  assert(failedClose.press(100));
  failedClose.update(false, true, 200);
  assert(failedClose.update(false, false, 5000) == Event::None);
  assert(failedClose.state() == State::Stopped);
  assert(failedClose.obstruction());
  assert(!failedClose.directionConfirmed());
  assert(!DoorWebCommandPolicy::shouldPress(failedClose, Command::Close));

  GarageDoorModel open(true);
  open.begin(false, 0);
  const GarageDoorModel::Position openPosition = {
      1, static_cast<uint8_t>(State::Open), static_cast<uint8_t>(State::Opening), 1, 0};
  assert(open.restorePosition(openPosition));
  assert(!DoorWebCommandPolicy::shouldPress(open, Command::Open));
  assert(DoorWebCommandPolicy::shouldPress(open, Command::Close));

  GarageDoorModel stoppedAfterClosing(true);
  stoppedAfterClosing.begin(false, 0);
  const GarageDoorModel::Position closingPosition = {
      1, static_cast<uint8_t>(State::Stopped), static_cast<uint8_t>(State::Closing), 1, 0};
  assert(stoppedAfterClosing.restorePosition(closingPosition));
  assert(DoorWebCommandPolicy::shouldPress(stoppedAfterClosing, Command::Open));
  assert(!DoorWebCommandPolicy::shouldPress(stoppedAfterClosing, Command::Close));

  GarageDoorModel stoppedAfterOpening(true);
  stoppedAfterOpening.begin(false, 0);
  const GarageDoorModel::Position openingPosition = {
      1, static_cast<uint8_t>(State::Stopped), static_cast<uint8_t>(State::Opening), 1, 0};
  assert(stoppedAfterOpening.restorePosition(openingPosition));
  assert(!DoorWebCommandPolicy::shouldPress(stoppedAfterOpening, Command::Open));
  assert(DoorWebCommandPolicy::shouldPress(stoppedAfterOpening, Command::Close));

  GarageDoorModel moving(true);
  moving.begin(true, 0);
  assert(moving.press(100));
  moving.update(false, true, 200);
  assert(DoorWebCommandPolicy::shouldPress(moving, Command::Stop));

  GarageDoorModel movingUnknown(true);
  movingUnknown.begin(false, 0);
  movingUnknown.update(false, true, 100);
  assert(!DoorWebCommandPolicy::shouldPress(movingUnknown, Command::Stop));
}

static void testUnconfirmedWebDirections() {
  using Command = DoorWebCommandPolicy::Command;

  GarageDoorModel obstructed(true);
  obstructed.begin(true, 0);
  obstructed.update(true, true, 100);
  obstructed.update(false, true, 200);
  obstructed.update(false, false, 16600);
  obstructed.update(false, true, 17000);
  obstructed.update(false, false, 20000);
  assert(obstructed.state() == State::Stopped);
  assert(obstructed.obstruction());
  assert(!obstructed.directionConfirmed());
  assert(!DoorWebCommandPolicy::shouldPress(obstructed, Command::Open));
  assert(!DoorWebCommandPolicy::shouldPress(obstructed, Command::Close));
  obstructed.update(false, true, 20100);
  obstructed.update(false, false, 36600);
  assert(obstructed.state() == State::Stopped);
  assert(!obstructed.directionConfirmed());
  assert(!DoorWebCommandPolicy::shouldPress(obstructed, Command::Open));
  assert(!DoorWebCommandPolicy::shouldPress(obstructed, Command::Close));

  GarageDoorModel earlyStop(true);
  earlyStop.begin(true, 0);
  earlyStop.update(true, true, 100);
  earlyStop.update(false, true, 200);
  earlyStop.update(false, false, 5000);
  assert(earlyStop.state() == State::Stopped);
  assert(!earlyStop.directionConfirmed());
  assert(!DoorWebCommandPolicy::shouldPress(earlyStop, Command::Open));
  assert(!DoorWebCommandPolicy::shouldPress(earlyStop, Command::Close));

  GarageDoorModel reversed(true);
  reversed.begin(true, 0);
  reversed.update(true, true, 100);
  reversed.update(false, true, 200);
  assert(reversed.press(1000));
  assert(reversed.update(false, true, 4000) == Event::Reversed);
  reversed.update(false, false, 6000);
  assert(reversed.state() == State::Stopped);
  assert(!reversed.directionConfirmed());
  assert(!DoorWebCommandPolicy::shouldPress(reversed, Command::Open));
  assert(!DoorWebCommandPolicy::shouldPress(reversed, Command::Close));

  GarageDoorModel openWithoutFlash(false);
  openWithoutFlash.begin(true, 0);
  assert(openWithoutFlash.press(100));
  openWithoutFlash.update(false, false, 15100);
  assert(openWithoutFlash.state() == State::Open);
  assert(!openWithoutFlash.directionConfirmed());
  assert(DoorWebCommandPolicy::shouldPress(openWithoutFlash, Command::Close));
}

static void testWebCommandReasons() {
  using Policy = DoorWebCommandPolicy;
  using Command = Policy::Command;
  using Action = Policy::Action;

  GarageDoorModel startup(true);
  startup.begin(false, 0);
  auto result = Policy::evaluate(startup, Command::Open);
  assert(result.action == Action::NoOp);
  assert(std::strcmp(result.reason, "already open") == 0);
  result = Policy::evaluate(startup, Command::Close);
  assert(result.action == Action::Press);
  result = Policy::evaluate(startup, Command::Stop);
  assert(result.action == Action::NoOp);
  assert(std::strcmp(result.reason, "already stopped") == 0);

  startup.update(true, false, 100);
  result = Policy::evaluate(startup, Command::Close);
  assert(result.action == Action::NoOp);
  assert(std::strcmp(result.reason, "already closed") == 0);
  assert(Policy::evaluate(startup, Command::Open).action == Action::Press);

  GarageDoorModel stopped(true);
  stopped.begin(false, 0);
  const GarageDoorModel::Position unconfirmed = {
      1, static_cast<uint8_t>(State::Stopped), static_cast<uint8_t>(State::Closing), 0, 1};
  assert(stopped.restorePosition(unconfirmed));
  result = Policy::evaluate(stopped, Command::Open);
  assert(result.action == Action::Reject);
  assert(std::strcmp(result.reason, "door direction unconfirmed") == 0);

  const GarageDoorModel::Position confirmed = {
      1, static_cast<uint8_t>(State::Stopped), static_cast<uint8_t>(State::Closing), 1, 0};
  assert(stopped.restorePosition(confirmed));
  assert(Policy::evaluate(stopped, Command::Open).action == Action::Press);
  result = Policy::evaluate(stopped, Command::Close);
  assert(result.action == Action::Reject);
  assert(std::strcmp(result.reason, "next press would move in opposite direction") == 0);
}

static void testStopWhilePending() {
  using Policy = DoorWebCommandPolicy;
  using Command = Policy::Command;
  using Action = Policy::Action;

  GarageDoorModel door(true);
  door.begin(true, 0);
  assert(door.press(100));
  door.update(false, false, 200);
  assert(door.state() == State::Opening);
  auto result = Policy::evaluate(door, Command::Stop);
  assert(result.action == Action::Reject);
  assert(std::strcmp(result.reason, "opener start or stop awaiting confirmation") == 0);
  door.update(false, false, 3099);
  assert(Policy::evaluate(door, Command::Stop).action == Action::Reject);

  door.update(false, true, 3100);
  assert(!door.pending());
  assert(Policy::evaluate(door, Command::Stop).action == Action::Press);
  assert(door.press(3200));
  result = Policy::evaluate(door, Command::Stop);
  assert(result.action == Action::Reject);
  assert(std::strcmp(result.reason, "opener start or stop awaiting confirmation") == 0);
  door.update(false, false, 5000);
  result = Policy::evaluate(door, Command::Stop);
  assert(result.action == Action::NoOp);
  assert(std::strcmp(result.reason, "already stopped") == 0);

  GarageDoorModel failedStart(true);
  failedStart.begin(true, 0);
  assert(failedStart.press(100));
  failedStart.update(false, false, 200);
  assert(failedStart.update(false, false, 3100) == Event::StartFailed);
  assert(Policy::evaluate(failedStart, Command::Stop).action == Action::NoOp);
}

static void testPositionRestore() {
  using Reset = DoorPositionRestore::Reset;
  using Decision = DoorPositionRestore::Decision;
  GarageDoorModel::Position saved = {1, static_cast<uint8_t>(State::Open),
                                     static_cast<uint8_t>(State::Closing), 1, 0};
  assert(DoorPositionRestore::decide(Reset::Software, false, true, saved) == Decision::Restore);
  assert(DoorPositionRestore::decide(Reset::Panic, false, true, saved) == Decision::Restore);
  assert(DoorPositionRestore::decide(Reset::InterruptWatchdog, false, true, saved) == Decision::Restore);
  assert(DoorPositionRestore::decide(Reset::TaskWatchdog, false, true, saved) == Decision::Restore);
  assert(DoorPositionRestore::decide(Reset::Watchdog, false, true, saved) == Decision::Restore);
  assert(DoorPositionRestore::decide(Reset::Usb, false, true, saved) == Decision::Restore);
  assert(DoorPositionRestore::decide(Reset::PowerOn, false, true, saved) == Decision::ResetNotEligible);
  assert(DoorPositionRestore::decide(Reset::Brownout, false, true, saved) == Decision::ResetNotEligible);
  assert(DoorPositionRestore::decide(Reset::Other, false, true, saved) == Decision::ResetNotEligible);
  assert(DoorPositionRestore::decide(Reset::Software, true, true, saved) == Decision::ReedClosed);
  assert(DoorPositionRestore::decide(Reset::Software, false, false, saved) == Decision::NoRecord);
  auto corrupt = saved;
  corrupt.directionConfirmed = 2;
  assert(DoorPositionRestore::decide(Reset::Software, false, true, corrupt) == Decision::InvalidRecord);

  GarageDoorModel restored(true);
  restored.begin(false, 0);
  assert(restored.restorePosition(saved));
  assert(!restored.startupDirectionUnknown());
  assert(restored.press(100));
  GarageDoorModel::Position snapshot;
  assert(!restored.position(snapshot));

  GarageDoorModel fallback(true);
  fallback.begin(false, 0);
  assert(fallback.startupDirectionUnknown());

  GarageDoorModel obstructed(true);
  obstructed.begin(false, 0);
  GarageDoorModel::Position unknown = {1, static_cast<uint8_t>(State::Stopped),
                                       static_cast<uint8_t>(State::Closing), 0, 1};
  assert(obstructed.restorePosition(unknown));
  assert(obstructed.position(snapshot));
  assert(snapshot.obstruction == 1 && snapshot.directionConfirmed == 0);

  GarageDoorModel closed(true);
  closed.begin(true, 0);
  assert(!closed.restorePosition(saved));
  assert(!closed.position(snapshot));
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
  testOpenCompletionMargin();
  testMotorStopBeforeMovementStart();
  testDoorWebCommands();
  testUnconfirmedWebDirections();
  testWebCommandReasons();
  testStopWhilePending();
  testTimeoutAndFallback();
  testReedPriorityAndFailedStart();
  testPositionRestore();
  std::puts("Garage door model tests passed");
}
