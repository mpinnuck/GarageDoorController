#pragma once

#include <stdint.h>

class MotionInput {
public:
  /**
   * @brief Updates the qualified motor-running state from the indicator input.
   * @param[in] lit True if the indicator is currently lit.
   * @param[in] now Current time in milliseconds.
   * @return True if the motor is considered running; otherwise, false.
   */
  bool update(bool lit, uint32_t now);

  /**
   * @brief Checks whether the motor is considered to be running.
   * @return True if running; otherwise, false.
   */
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
  struct Position {
    uint8_t version;
    uint8_t state;
    uint8_t lastDirection;
    uint8_t directionConfirmed;
    uint8_t obstruction;
  };
  static constexpr uint32_t kOpenTravelMs = 15000;
  static constexpr uint32_t kOpenCompletionMs = 13000;
  static constexpr uint32_t kCloseTravelMs = 13500;
  static constexpr uint32_t kStartTimeoutMs = 3000;

  /**
   * @brief Creates a door model configured for the opener's motion indicator.
   * @param[in] flashFitted True if the opener's motor indicator is connected.
   */
  explicit GarageDoorModel(bool flashFitted) : _flashFitted(flashFitted) {}

  /**
   * @brief Initializes the model from the door's reed-switch state.
   * @param[in] closed True if the door is physically at the closed position.
   * @param[in] now Current time in milliseconds.
   */
  void begin(bool closed, uint32_t now);

  /**
   * @brief Checks whether the model can accept an opener-button press.
   * @return True if a press can be accepted; otherwise, false.
   */
  bool canPress() const;

  /**
   * @brief Records an opener-button press and updates the estimated door state.
   * @param[in] now Current time in milliseconds.
   * @return True if the press was accepted; otherwise, false.
   */
  bool press(uint32_t now);

  /**
   * @brief Updates the model using sensor readings and elapsed time.
   * @param[in] closed True if the closed-position reed switch is active.
   * @param[in] motorRunning True if the opener's motor indicator is active.
   * @param[in] now Current time in milliseconds.
   * @return An event raised during this update, or Event::None.
   */
  Event update(bool closed, bool motorRunning, uint32_t now);

  /**
   * @brief Gets the current door state, preferring the closed reed-switch reading.
   * @return The effective door state.
   */
  State state() const { return _closed ? State::Closed : _state; }

  /**
   * @brief Gets the model's movement state without applying the reed override.
   * @return The model's current movement state.
   */
  State movementState() const { return _state; }

  /**
   * @brief Checks whether the model currently considers the door to be moving.
   * @return True if moving; otherwise, false.
   */
  bool moving() const;

  /**
   * @brief Checks whether an obstruction has been detected.
   * @return True if obstructed; otherwise, false.
   */
  bool obstruction() const { return _obstruction; }

  /**
   * @brief Checks whether an opener start or stop is awaiting confirmation.
   * @return True if an action is pending; otherwise, false.
   */
  bool pending() const { return _pending != Pending::None; }

  /**
   * @brief Checks whether the motor indicator currently reports a running motor.
   * @return True if the motor is running; otherwise, false.
   */
  bool motorRunning() const { return _motor; }

  /**
   * @brief Checks whether a motor indicator is configured for the opener.
   * @return True if the indicator is fitted; otherwise, false.
   */
  bool flashFitted() const { return _flashFitted; }

  /**
   * @brief Checks whether the door direction is unknown after startup.
   * @return True if startup direction remains unknown; otherwise, false.
   */
  bool startupDirectionUnknown() const { return _startupDirectionUnknown; }

  /**
   * @brief Checks whether the last movement direction is confirmed.
   * @return True if direction is confirmed; otherwise, false.
   */
  bool directionConfirmed() const { return _directionConfirmed; }

  /**
   * @brief Estimates the direction of the next opener-button press.
   * @return The estimated next direction, or State::MovingUnknown if unknown.
   */
  State nextDirection() const;

  /**
   * @brief Creates a persistable snapshot of the current door position.
   * @param[out] position Receives the position snapshot when available.
   * @return True if a valid snapshot was produced; otherwise, false.
   */
  bool position(Position &position) const;

  /**
   * @brief Restores the model from a saved position snapshot.
   * @param[in] position Saved position to validate and restore.
   * @return True if the position was restored; otherwise, false.
   */
  bool restorePosition(const Position &position);

  /**
   * @brief Validates the fields and state combinations in a saved position.
   * @param[in] position Position record to validate.
   * @return True if the record is valid; otherwise, false.
   */
  static bool validPosition(const Position &position);

  /**
   * @brief Gets the display name for a door state.
   * @param[in] state Door state to describe.
   * @return A constant string describing the state.
   */
  static const char *name(State state);

private:
  enum class Pending { None, Start, Stop };

  /**
   * @brief Checks whether a state represents door movement.
   * @param[in] state State to check.
   * @return True if the state represents movement; otherwise, false.
   */
  static bool isMoving(State state);

  /**
   * @brief Begins tracking movement in the specified direction.
   * @param[in] direction Direction of movement.
   * @param[in] now Current time in milliseconds.
   */
  void start(State direction, uint32_t now);

  /**
   * @brief Resolves the model state when movement ends.
   * @param[in] now Current time in milliseconds.
   * @param[in] commandedStop True if movement ended after a stop command.
   */
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
  bool _startupDirectionUnknown = false;
  bool _directionConfirmed = false;
  bool _candidateDirectionKnown = false;
  bool _previousDirectionConfirmed = false;
  bool _previousStartupDirectionUnknown = false;
};
