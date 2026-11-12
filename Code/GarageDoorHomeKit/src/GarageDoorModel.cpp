#include "GarageDoorModel.h"

bool MotionInput::update(bool lit, uint32_t now) {
  if (lit != _lit) {
    if (!lit && !_qualifiedLit && now - _litSince >= 50) {
      _qualifiedLit = true;
      _running = true;
    }
    _lit = lit;
    if (lit) _litSince = now;
    else if (_qualifiedLit) {
      _qualifiedLit = false;
      _unlitSince = now;
    }
  }
  if (_lit && !_qualifiedLit && now - _litSince >= 50) {
    _qualifiedLit = true;
    _running = true;
  }
  if (_running && !_qualifiedLit && now - _unlitSince >= 1500) _running = false;
  return _running;
}

bool GarageDoorModel::isMoving(State state) {
  return state == State::Opening || state == State::Closing || state == State::MovingUnknown;
}

bool GarageDoorModel::moving() const { return isMoving(_state); }

void GarageDoorModel::begin(bool closed, uint32_t now) {
  _closed = closed;
  _state = closed ? State::Closed : State::Stopped;
  _movementStart = now;
}

GarageDoorModel::State GarageDoorModel::nextDirection() const {
  if (_closed || _state == State::Closed) return State::Opening;
  if (_state == State::Open) return State::Closing;
  if (_lastDirection == State::Opening) return State::Closing;
  if (_lastDirection == State::Closing) return State::Opening;
  return State::MovingUnknown;
}

void GarageDoorModel::start(State direction, uint32_t now) {
  _state = direction;
  if (direction == State::Opening || direction == State::Closing) _lastDirection = direction;
  _movementStart = now;
  _obstruction = false;
}

bool GarageDoorModel::canPress() const {
  return _pending == Pending::None && !_ignoreMotorUntilOff && !(_closed && _motor);
}

bool GarageDoorModel::press(uint32_t now) {
  if (!canPress()) return false;
  _pressTime = now;
  if (isMoving(_state)) {
    if (_flashFitted) _pending = Pending::Stop;
    else {
      _state = State::Stopped;
      _obstruction = false;
    }
  } else {
    _previous = _state;
    _previousDirection = _lastDirection;
    _previousObstruction = _obstruction;
    start(nextDirection(), now);
    if (_flashFitted) _pending = Pending::Start;
  }
  return true;
}

void GarageDoorModel::finish(uint32_t now, bool commandedStop) {
  if (_closed) _state = State::Closed;
  else if (commandedStop) _state = State::Stopped;
  else if (_state == State::Opening) _state = State::Open;
  else if (_state == State::Closing && now - _movementStart >= kCloseTravelMs) {
    _state = State::Open;
    _obstruction = true;
  } else _state = State::Stopped;
}

GarageDoorModel::Event GarageDoorModel::update(bool closed, bool motorRunning, uint32_t now) {
  Event event = Event::None;
  const bool wasClosed = _closed;
  _closed = closed;

  if (closed && !wasClosed) {
    _state = State::Closed;
    _pending = Pending::None;
    _obstruction = false;
    _ignoreMotorUntilOff = motorRunning;
  } else if (!closed && wasClosed && !_flashFitted && !isMoving(_state)) {
    start(State::Opening, now);
  }

  if (_flashFitted) {
    if (!motorRunning) _ignoreMotorUntilOff = false;
    if (motorRunning && !_motor && !_ignoreMotorUntilOff) {
      if (_pending == Pending::Start) _pending = Pending::None;
      else if (!isMoving(_state)) start(nextDirection(), now);
    }
    if (!motorRunning && _motor && !_ignoreMotorUntilOff) {
      if (isMoving(_state)) finish(now - 1500, _pending == Pending::Stop);
      _pending = Pending::None;
    }
    if (_pending == Pending::Start && now - _pressTime >= kStartTimeoutMs) {
      _state = !_closed && _previous == State::Closed ? State::Stopped : _previous;
      _lastDirection = _previousDirection;
      _obstruction = _previousObstruction;
      _pending = Pending::None;
      event = Event::StartFailed;
    } else if (_pending == Pending::Stop && now - _pressTime >= kStartTimeoutMs) {
      const State reverse = _state == State::Opening ? State::Closing :
                            _state == State::Closing ? State::Opening : State::MovingUnknown;
      start(reverse, now);
      _pending = Pending::None;
      event = Event::Reversed;
    }
    _motor = motorRunning;
  }

  if (isMoving(_state) && (!_flashFitted || _pending != Pending::Start)) {
    const uint32_t limit = _flashFitted ? 2 * kOpenTravelMs :
                           _state == State::Closing ? kCloseTravelMs : kOpenTravelMs;
    if (now - _movementStart >= limit) {
      if (_flashFitted) {
        _state = closed ? State::Closed : State::Stopped;
        _ignoreMotorUntilOff = motorRunning;
        event = Event::SafetyTimeout;
      } else {
        finish(now, false);
      }
      _pending = Pending::None;
    }
  }
  return event;
}

const char *GarageDoorModel::name(State state) {
  switch (state) {
    case State::Closed: return "Closed";
    case State::Open: return "Open";
    case State::Stopped: return "Stopped";
    case State::Opening: return "Opening";
    case State::Closing: return "Closing";
    case State::MovingUnknown: return "Moving";
  }
  return "Unknown";
}
