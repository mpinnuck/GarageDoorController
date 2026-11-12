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
  _startupDirectionUnknown = !closed;
}

bool GarageDoorModel::validPosition(const Position &position) {
  const auto state = static_cast<State>(position.state);
  const auto direction = static_cast<State>(position.lastDirection);
  return position.version == 1 && (state == State::Open || state == State::Stopped) &&
         (direction == State::Opening || direction == State::Closing ||
          direction == State::MovingUnknown) &&
         position.directionConfirmed <= 1 && position.obstruction <= 1 &&
         (!position.directionConfirmed || direction != State::MovingUnknown) &&
         (state != State::Open || (position.directionConfirmed && !position.obstruction));
}

bool GarageDoorModel::position(Position &position) const {
  if (_closed || moving() || pending() || _motor || _ignoreMotorUntilOff ||
      _startupDirectionUnknown) return false;
  position = {1, static_cast<uint8_t>(_state), static_cast<uint8_t>(_lastDirection),
              static_cast<uint8_t>(_directionConfirmed), static_cast<uint8_t>(_obstruction)};
  return validPosition(position);
}

bool GarageDoorModel::restorePosition(const Position &position) {
  if (_closed || moving() || pending() || _motor || !validPosition(position)) return false;
  _state = static_cast<State>(position.state);
  _lastDirection = static_cast<State>(position.lastDirection);
  _directionConfirmed = position.directionConfirmed;
  _obstruction = position.obstruction;
  _startupDirectionUnknown = false;
  return true;
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

bool GarageDoorModel::press(uint32_t now, bool assumeStartupClose) {
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
    _previousDirectionConfirmed = _directionConfirmed;
    _previousStartupDirectionUnknown = _startupDirectionUnknown;
    _previousObstruction = _obstruction;
    _candidateDirectionKnown = _closed ||
        ((_state == State::Open || _state == State::Stopped) && _directionConfirmed);
    const State direction = assumeStartupClose && _startupDirectionUnknown
                                ? State::Closing : nextDirection();
    _startupDirectionUnknown = false;
    _directionConfirmed = false;
    start(direction, now);
    if (_flashFitted) _pending = Pending::Start;
  }
  return true;
}

void GarageDoorModel::finish(uint32_t now, bool commandedStop) {
  if (_closed) _state = State::Closed;
  else if (commandedStop) _state = State::Stopped;
  else if (_state == State::Opening &&
           now - _movementStart >= (_flashFitted ? kOpenCompletionMs : kOpenTravelMs) &&
           (!_flashFitted || _directionConfirmed))
    _state = State::Open;
  else if (_state == State::Closing) {
    _state = State::Stopped;
    _obstruction = true;
    _directionConfirmed = false;
  } else {
    _state = State::Stopped;
    _directionConfirmed = false;
  }
}

GarageDoorModel::Event GarageDoorModel::update(bool closed, bool motorRunning, uint32_t now) {
  Event event = Event::None;
  const bool wasClosed = _closed;
  _closed = closed;

  if (closed && !wasClosed) {
    _startupDirectionUnknown = false;
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
      else if (!isMoving(_state)) {
        _candidateDirectionKnown = _closed ||
            ((_state == State::Open || _state == State::Stopped) && _directionConfirmed);
        start(nextDirection(), now);
      }
      _startupDirectionUnknown = false;
      _directionConfirmed = _candidateDirectionKnown &&
          (_state == State::Opening || _state == State::Closing);
    }
    if (!motorRunning && _motor && !_ignoreMotorUntilOff) {
      if (isMoving(_state)) finish(now - 1500, _pending == Pending::Stop);
      _pending = Pending::None;
    }
    if (_pending == Pending::Start && now - _pressTime >= kStartTimeoutMs) {
      _state = !_closed && _previous == State::Closed ? State::Stopped : _previous;
      _lastDirection = _previousDirection;
      _directionConfirmed = _previousDirectionConfirmed;
      _startupDirectionUnknown = !_closed && _previousStartupDirectionUnknown;
      _obstruction = _previousObstruction;
      _pending = Pending::None;
      event = Event::StartFailed;
    } else if (_pending == Pending::Stop && now - _pressTime >= kStartTimeoutMs) {
      const State reverse = _state == State::Opening ? State::Closing :
                            _state == State::Closing ? State::Opening : State::MovingUnknown;
      start(reverse, now);
      _directionConfirmed = false;
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
        _directionConfirmed = false;
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
