#include "GarageDoorModel.h"
#include "DoorWebCommandPolicy.h"
#include "HomeKitDoorPolicy.h"
#include "DoorPositionRestore.h"

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
  assert(closing.directionConfirmed());
  assert(HomeKitDoorPolicy::action(closing, HomeKitDoorPolicy::Target::Open, true) ==
         HomeKitDoorPolicy::Action::Press);
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
  using Policy = HomeKitDoorPolicy;

  GarageDoorModel shortMovement(true);
  shortMovement.begin(true, 0);
  shortMovement.update(true, true, 100);
  shortMovement.update(false, true, 200);
  shortMovement.update(false, false, 14599);
  assert(shortMovement.state() == State::Stopped);
  assert(!shortMovement.directionConfirmed());
  assert(Policy::action(shortMovement, Policy::Target::Closed, true) == Policy::Action::Reject);

  GarageDoorModel fullMovement(true);
  fullMovement.begin(true, 0);
  fullMovement.update(true, true, 100);
  fullMovement.update(false, true, 200);
  fullMovement.update(false, false, 14600);
  assert(fullMovement.state() == State::Open);
  assert(Policy::action(fullMovement, Policy::Target::Closed, true) == Policy::Action::Press);

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

static void testDoorWebCommands() {
  using Command = DoorWebCommandPolicy::Command;

  GarageDoorModel closed(true);
  closed.begin(true, 0);
  assert(DoorWebCommandPolicy::shouldPress(closed, Command::Open));
  assert(!DoorWebCommandPolicy::shouldPress(closed, Command::Close));
  assert(!DoorWebCommandPolicy::shouldPress(closed, Command::Stop));

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
      1, static_cast<uint8_t>(State::Stopped), static_cast<uint8_t>(State::Closing), 0, 1};
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

static void testHomeKitRequests() {
  using Policy = HomeKitDoorPolicy;
  using Target = Policy::Target;
  using Action = Policy::Action;
  using Current = Policy::Current;

  GarageDoorModel door(true);
  door.begin(true, 0);
  assert(Policy::current(door) == Current::Closed);
  assert(Policy::action(door, Target::Open, false) == Action::Reject);
  assert(Policy::action(door, Target::Closed, true) == Action::NoOp);
  assert(Policy::action(door, Target::Open, true) == Action::Press);
  assert(door.press(100));
  assert(Policy::action(door, Target::Open, true) == Action::Reject);
  assert(door.update(true, false, 3100) == Event::StartFailed);
  assert(door.state() == State::Closed);
  assert(door.press(4000));
  door.update(true, true, 4100);
  assert(Policy::current(door) == Current::Closed);
  assert(Policy::action(door, Target::Open, true) == Action::NoOp);
  assert(Policy::action(door, Target::Closed, true) == Action::Reject);
  door.update(false, true, 5000);
  assert(Policy::current(door) == Current::Opening);
  assert(door.press(6000));
  assert(Policy::action(door, Target::Open, true) == Action::Reject);
  door.update(false, false, 8000);
  assert(Policy::current(door) == Current::Stopped);
  assert(Policy::action(door, Target::Open, true) == Action::Reject);
  assert(Policy::action(door, Target::Closed, true) == Action::Press);
  assert(door.press(9000));
  door.update(false, true, 9100);
  assert(Policy::current(door) == Current::Closing);
  assert(Policy::action(door, Target::Closed, true) == Action::NoOp);
  assert(Policy::action(door, Target::Open, true) == Action::Reject);
  door.update(false, false, 12000);
  assert(Policy::current(door) == Current::Stopped);
  assert(door.obstruction());
  assert(!door.directionConfirmed());
  assert(Policy::action(door, Target::Open, true) == Action::Reject);
  assert(Policy::action(door, Target::Closed, true) == Action::Reject);
  door.update(false, true, 13000);
  assert(Policy::current(door) == Current::Stopped);
  assert(Policy::action(door, Target::Open, true) == Action::Reject);
  assert(Policy::action(door, Target::Closed, true) == Action::Reject);
  door.update(true, true, 14000);
  door.update(true, false, 15500);
  assert(Policy::action(door, Target::Open, true) == Action::Press);

  GarageDoorModel startup(true);
  startup.begin(false, 0);
  assert(Policy::action(startup, Target::Open, true) == Action::Reject);
  assert(Policy::action(startup, Target::Closed, true) == Action::PressAssumingStartupClose);
  assert(startup.press(100, true));
  assert(startup.update(false, false, 3100) == Event::StartFailed);
  assert(Policy::action(startup, Target::Closed, true) == Action::PressAssumingStartupClose);
  assert(startup.press(4000, true));
  startup.update(false, true, 4100);
  assert(Policy::current(startup) == Current::Stopped);
  assert(!startup.directionConfirmed());
  startup.update(true, true, 6000);
  assert(Policy::current(startup) == Current::Closed);

  GarageDoorModel wrongGuess(true);
  wrongGuess.begin(false, 0);
  assert(wrongGuess.press(100, true));
  wrongGuess.update(false, true, 200);
  assert(Policy::current(wrongGuess) == Current::Stopped);
  wrongGuess.update(false, false, 16600);
  assert(wrongGuess.state() == State::Stopped);
  assert(!wrongGuess.directionConfirmed());
  assert(Policy::action(wrongGuess, Target::Open, true) == Action::Reject);
  assert(Policy::action(wrongGuess, Target::Closed, true) == Action::Reject);
  GarageDoorModel::Position guessedPosition;
  assert(wrongGuess.position(guessedPosition));
  GarageDoorModel afterRestart(true);
  afterRestart.begin(false, 17000);
  assert(afterRestart.restorePosition(guessedPosition));
  assert(Policy::action(afterRestart, Target::Open, true) == Action::Reject);
  assert(Policy::action(afterRestart, Target::Closed, true) == Action::Reject);

  GarageDoorModel unknown(true);
  unknown.begin(false, 0);
  unknown.update(false, true, 100);
  assert(Policy::current(unknown) == Current::Stopped);
  assert(Policy::action(unknown, Target::Closed, true) == Action::Reject);
  unknown.update(false, false, 2000);
  assert(Policy::action(unknown, Target::Closed, true) == Action::Reject);

  GarageDoorModel noLamp(false);
  noLamp.begin(true, 0);
  assert(Policy::action(noLamp, Target::Closed, true) == Action::NoOp);
  assert(Policy::action(noLamp, Target::Open, true) == Action::Reject);

  GarageDoorModel uncertain(true);
  uncertain.begin(true, 0);
  uncertain.update(true, true, 100);
  uncertain.update(false, true, 200);
  assert(uncertain.press(1000));
  assert(uncertain.update(false, true, 4000) == Event::Reversed);
  assert(Policy::current(uncertain) == Current::Stopped);
  assert(Policy::action(uncertain, Target::Closed, true) == Action::Reject);
  uncertain.update(false, false, 6000);
  assert(Policy::action(uncertain, Target::Open, true) == Action::Reject);
  uncertain.update(false, true, 7000);
  assert(Policy::current(uncertain) == Current::Stopped);
  assert(Policy::action(uncertain, Target::Open, true) == Action::Reject);
  uncertain.update(false, false, 25000);
  assert(uncertain.state() == State::Stopped);

  GarageDoorModel earlyStop(true);
  earlyStop.begin(true, 0);
  earlyStop.update(true, true, 100);
  earlyStop.update(false, true, 200);
  earlyStop.update(false, false, 5000);
  assert(earlyStop.state() == State::Stopped);
  assert(!earlyStop.directionConfirmed());
  assert(Policy::action(earlyStop, Target::Open, true) == Action::Reject);
  assert(Policy::action(earlyStop, Target::Closed, true) == Action::Reject);

  GarageDoorModel autoReverse(true);
  autoReverse.begin(true, 0);
  autoReverse.update(true, true, 100);
  autoReverse.update(false, true, 200);
  autoReverse.update(false, false, 16600);
  autoReverse.update(false, true, 17000);
  autoReverse.update(false, true, 44000);
  autoReverse.update(false, false, 44500);
  assert(autoReverse.state() == State::Stopped);
  assert(autoReverse.obstruction());
  assert(!autoReverse.directionConfirmed());
  assert(Policy::action(autoReverse, Target::Open, true) == Action::Reject);
  assert(Policy::action(autoReverse, Target::Closed, true) == Action::Reject);
}

static void testPositionRestore() {
  using Reset = DoorPositionRestore::Reset;
  using Decision = DoorPositionRestore::Decision;
  using Policy = HomeKitDoorPolicy;
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
  assert(Policy::action(restored, Policy::Target::Closed, true) == Policy::Action::Press);
  assert(restored.press(100));
  GarageDoorModel::Position snapshot;
  assert(!restored.position(snapshot));

  GarageDoorModel fallback(true);
  fallback.begin(false, 0);
  assert(Policy::action(fallback, Policy::Target::Closed, true) ==
         Policy::Action::PressAssumingStartupClose);

  GarageDoorModel obstructed(true);
  obstructed.begin(false, 0);
  GarageDoorModel::Position unknown = {1, static_cast<uint8_t>(State::Stopped),
                                       static_cast<uint8_t>(State::Closing), 0, 1};
  assert(obstructed.restorePosition(unknown));
  assert(obstructed.position(snapshot));
  assert(snapshot.obstruction == 1 && snapshot.directionConfirmed == 0);
  assert(Policy::action(obstructed, Policy::Target::Open, true) == Policy::Action::Reject);
  assert(Policy::action(obstructed, Policy::Target::Closed, true) == Policy::Action::Reject);

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
  testDoorWebCommands();
  testTimeoutAndFallback();
  testReedPriorityAndFailedStart();
  testHomeKitRequests();
  testPositionRestore();
  std::puts("Garage door model tests passed");
}
