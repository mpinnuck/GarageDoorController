#pragma once

#include <stdint.h>

class MotionInput {
public:
  bool update(bool lit, uint32_t now);
  bool running() const { return _running; }

private:
  bool _lit = false;
  bool _qualifiedLit = false;
  bool _running = false;
  uint32_t _litSince = 0;
  uint32_t _unlitSince = 0;
};

class GarageDoorModel {
public:
  enum class State { Closed, Open, Stopped, Opening, Closing, MovingUnknown };
  enum class Event { None, StartFailed, Reversed, SafetyTimeout };
  static constexpr uint32_t kOpenTravelMs = 15000;
  static constexpr uint32_t kCloseTravelMs = 13500;
  static constexpr uint32_t kStartTimeoutMs = 3000;

  explicit GarageDoorModel(bool flashFitted) : _flashFitted(flashFitted) {}
  void begin(bool closed, uint32_t now);
  bool canPress() const;
  bool press(uint32_t now);
  Event update(bool closed, bool motorRunning, uint32_t now);
  State state() const { return _closed ? State::Closed : _state; }
  bool moving() const;
  bool obstruction() const { return _obstruction; }
  bool pending() const { return _pending != Pending::None; }
  static const char *name(State state);

private:
  enum class Pending { None, Start, Stop };
  static bool isMoving(State state);
  State nextDirection() const;
  void start(State direction, uint32_t now);
  void finish(uint32_t now, bool commandedStop);

  const bool _flashFitted;
  State _state = State::Stopped;
  State _previous = State::Stopped;
  State _lastDirection = State::MovingUnknown;
  State _previousDirection = State::MovingUnknown;
  Pending _pending = Pending::None;
  uint32_t _movementStart = 0;
  uint32_t _pressTime = 0;
  bool _closed = false;
  bool _motor = false;
  bool _obstruction = false;
  bool _previousObstruction = false;
  bool _ignoreMotorUntilOff = false;
};
