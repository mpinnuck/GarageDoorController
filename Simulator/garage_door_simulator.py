#!/usr/bin/env python3
"""
Garage Door Controller simulator (mirrors firmware 4.3.0).

Serves the controller's own web page and HTTP API, so browsers, curl and iOS
Shortcuts can be developed without the real controller or door.

Layers, bottom to top:
  World     the physical door, the Padors opener (wall-button input, LAMP output)
            and the reed switch
  Firmware  a line-for-line port of the controller code: DebouncedInput,
            MotionInput, GarageDoorModel, DoorWebCommandPolicy, DoorControlGate,
            DoorPositionRestore and the main loop
  HTTP      the same endpoints, headers, status codes and JSON as StatusWebServer,
            serving the same HTML as StatusPage.h

Only python 3 standard library is needed.

    python3 garage_door_simulator.py --enable-control
    open http://localhost:8080/        (the controller's page)
    open http://localhost:8080/sim     (simulator panel: obstruction, reboot, ...)

Keep it in step with the firmware: point --page at the firmware's StatusPage.h
(or at an .html file) to serve the current page.
"""

import argparse
import json
import os
import re
import secrets
import socket
import sys
import threading
import time
from enum import IntEnum
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

FIRMWARE_VERSION = "4.3.0"
HERE = os.path.dirname(os.path.abspath(__file__))

# ---------------------------------------------------------------------------
# Firmware constants (main.cpp)
# ---------------------------------------------------------------------------
DEBOUNCE_MS = 500
OPENER_PULSE_MS = 500
DEVICE_NAME = "garagecontroller"
SSID = "MicroConcepts-2G"
RSSI = -58
OPENER_FLASH_FITTED = True
POSITION_SETTLE_MS = 1500


# ---------------------------------------------------------------------------
# MotionInput / GarageDoorModel (GarageDoorModel.cpp)
# ---------------------------------------------------------------------------
def sub(a, b):
    """uint32_t subtraction, as the firmware does it (wraps instead of going negative)."""
    return (a - b) & 0xFFFFFFFF


class MotionInput:
    def __init__(self):
        self.lit = False
        self.qualified_lit = False
        self.running = False
        self.lit_since = 0
        self.unlit_since = 0

    def update(self, lit, now):
        if lit != self.lit:
            if not lit and not self.qualified_lit and sub(now, self.lit_since) >= 50:
                self.qualified_lit = True
                self.running = True
            self.lit = lit
            if lit:
                self.lit_since = now
            elif self.qualified_lit:
                self.qualified_lit = False
                self.unlit_since = now
        if self.lit and not self.qualified_lit and sub(now, self.lit_since) >= 50:
            self.qualified_lit = True
            self.running = True
        if self.running and not self.qualified_lit and sub(now, self.unlit_since) >= 1500:
            self.running = False
        return self.running


class State(IntEnum):
    Closed = 0
    Open = 1
    Stopped = 2
    Opening = 3
    Closing = 4
    MovingUnknown = 5


class Event(IntEnum):
    NoEvent = 0
    StartFailed = 1
    Reversed = 2
    SafetyTimeout = 3


NONE, START, STOP = 0, 1, 2  # Pending


def state_name(state):
    return {State.Closed: "Closed", State.Open: "Open", State.Stopped: "Stopped",
            State.Opening: "Opening", State.Closing: "Closing",
            State.MovingUnknown: "Moving"}.get(state, "Unknown")


class GarageDoorModel:
    OPEN_TRAVEL_MS = 15000
    OPEN_COMPLETION_MS = 13000
    CLOSE_TRAVEL_MS = 13500
    START_TIMEOUT_MS = 3000

    def __init__(self, flash_fitted):
        self.flash_fitted = flash_fitted
        self._state = State.Stopped
        self._previous = State.Stopped
        self._last_direction = State.MovingUnknown
        self._previous_direction = State.MovingUnknown
        self._pending = NONE
        self._movement_start = 0
        self._press_time = 0
        self._closed = False
        self._motor = False
        self._obstruction = False
        self._previous_obstruction = False
        self._ignore_motor_until_off = False
        self._startup_direction_unknown = False
        self._direction_confirmed = False
        self._candidate_direction_known = False
        self._previous_direction_confirmed = False
        self._previous_startup_direction_unknown = False

    # -- queries
    @staticmethod
    def is_moving(state):
        return state in (State.Opening, State.Closing, State.MovingUnknown)

    def moving(self):
        return self.is_moving(self._state)

    def state(self):
        return State.Closed if self._closed else self._state

    def obstruction(self):
        return self._obstruction

    def pending(self):
        return self._pending != NONE

    def motor_running(self):
        return self._motor

    def startup_direction_unknown(self):
        return self._startup_direction_unknown

    def direction_confirmed(self):
        return self._direction_confirmed

    def can_press(self):
        return (self._pending == NONE and not self._ignore_motor_until_off
                and not (self._closed and self._motor))

    def next_direction(self):
        if self._closed or self._state == State.Closed:
            return State.Opening
        if self._state == State.Open:
            return State.Closing
        if self._last_direction == State.Opening:
            return State.Closing
        if self._last_direction == State.Closing:
            return State.Opening
        return State.MovingUnknown

    # -- position persistence
    def begin(self, closed, now):
        self._closed = closed
        self._state = State.Closed if closed else State.Open
        self._movement_start = now
        self._startup_direction_unknown = not closed

    @staticmethod
    def valid_position(p):
        version, state, direction, confirmed, obstruction = p
        try:
            state = State(state)
            direction = State(direction)
        except ValueError:
            return False
        return (version == 1 and state in (State.Open, State.Stopped)
                and direction in (State.Opening, State.Closing, State.MovingUnknown)
                and confirmed <= 1 and obstruction <= 1
                and (not confirmed or direction != State.MovingUnknown)
                and (state != State.Open or (confirmed and not obstruction)))

    def position(self):
        if (self._closed or self.moving() or self.pending() or self._motor
                or self._ignore_motor_until_off or self._startup_direction_unknown):
            return None
        p = (1, int(self._state), int(self._last_direction),
             int(self._direction_confirmed), int(self._obstruction))
        return p if self.valid_position(p) else None

    def restore_position(self, p):
        if (self._closed or self.moving() or self.pending() or self._motor
                or not self.valid_position(p)):
            return False
        self._state = State(p[1])
        self._last_direction = State(p[2])
        self._direction_confirmed = bool(p[3])
        self._obstruction = bool(p[4])
        self._startup_direction_unknown = False
        return True

    # -- behaviour
    def _start(self, direction, now):
        self._state = direction
        if direction in (State.Opening, State.Closing):
            self._last_direction = direction
        self._movement_start = now
        self._obstruction = False

    def press(self, now):
        if not self.can_press():
            return False
        self._press_time = now
        if self.is_moving(self._state):
            if self.flash_fitted:
                self._pending = STOP
            else:
                self._state = State.Stopped
                self._obstruction = False
        else:
            self._previous = self._state
            self._previous_direction = self._last_direction
            self._previous_direction_confirmed = self._direction_confirmed
            self._previous_startup_direction_unknown = self._startup_direction_unknown
            self._previous_obstruction = self._obstruction
            self._candidate_direction_known = (
                self._closed or self._state == State.Open
                or (self._state == State.Stopped and self._direction_confirmed))
            direction = self.next_direction()
            self._startup_direction_unknown = False
            self._direction_confirmed = False
            self._start(direction, now)
            if self.flash_fitted:
                self._pending = START
        return True

    def _finish(self, now, commanded_stop):
        if self._closed:
            self._state = State.Closed
        elif commanded_stop:
            self._state = State.Stopped
        elif (self._state == State.Opening
              and sub(now, self._movement_start) >= (self.OPEN_COMPLETION_MS if self.flash_fitted
                                                  else self.OPEN_TRAVEL_MS)
              and (not self.flash_fitted or self._direction_confirmed)):
            self._state = State.Open
        elif self._state == State.Closing:
            self._state = State.Stopped
            self._obstruction = True
            self._direction_confirmed = False
        else:
            self._state = State.Stopped
            self._direction_confirmed = False

    def update(self, closed, motor_running, now):
        event = Event.NoEvent
        was_closed = self._closed
        self._closed = closed

        if closed and not was_closed:
            self._startup_direction_unknown = False
            self._state = State.Closed
            self._pending = NONE
            self._obstruction = False
            self._ignore_motor_until_off = motor_running
        elif (not closed and was_closed and not self.flash_fitted
              and not self.is_moving(self._state)):
            self._start(State.Opening, now)

        if self.flash_fitted:
            if not motor_running:
                self._ignore_motor_until_off = False
            if motor_running and not self._motor and not self._ignore_motor_until_off:
                if self._pending == START:
                    self._pending = NONE
                elif not self.is_moving(self._state):
                    self._candidate_direction_known = self._closed or (
                        self._state in (State.Open, State.Stopped) and self._direction_confirmed)
                    self._start(State.MovingUnknown if self._startup_direction_unknown
                                else self.next_direction(), now)
                self._startup_direction_unknown = False
                self._direction_confirmed = (
                    self._candidate_direction_known
                    and self._state in (State.Opening, State.Closing))
            if not motor_running and self._motor and not self._ignore_motor_until_off:
                if self.is_moving(self._state):
                    since_start = sub(now, self._movement_start)
                    stopped_at = sub(now, 1500) if since_start > 1500 else self._movement_start
                    self._finish(stopped_at, self._pending == STOP)
                self._pending = NONE
            if self._pending == START and sub(now, self._press_time) >= self.START_TIMEOUT_MS:
                self._state = (State.Stopped if (not self._closed and self._previous == State.Closed)
                               else self._previous)
                self._last_direction = self._previous_direction
                self._direction_confirmed = self._previous_direction_confirmed
                self._startup_direction_unknown = (not self._closed
                                                   and self._previous_startup_direction_unknown)
                self._obstruction = self._previous_obstruction
                self._pending = NONE
                event = Event.StartFailed
            elif self._pending == STOP and sub(now, self._press_time) >= self.START_TIMEOUT_MS:
                reverse = (State.Closing if self._state == State.Opening else
                           State.Opening if self._state == State.Closing else State.MovingUnknown)
                self._start(reverse, now)
                self._direction_confirmed = False
                self._pending = NONE
                event = Event.Reversed
            self._motor = motor_running

        if self.is_moving(self._state) and (not self.flash_fitted or self._pending != START):
            limit = (2 * self.OPEN_TRAVEL_MS if self.flash_fitted else
                     self.CLOSE_TRAVEL_MS if self._state == State.Closing else self.OPEN_TRAVEL_MS)
            if sub(now, self._movement_start) >= limit:
                if self.flash_fitted:
                    self._state = State.Closed if closed else State.Stopped
                    self._direction_confirmed = False
                    self._ignore_motor_until_off = motor_running
                    event = Event.SafetyTimeout
                else:
                    self._finish(now, False)
                self._pending = NONE
        return event


# ---------------------------------------------------------------------------
# DoorWebCommandPolicy.h
# ---------------------------------------------------------------------------
class Cmd(IntEnum):
    Open = 0
    Close = 1
    Stop = 2


PRESS, NOOP, REJECT, FAILED = "Press", "NoOp", "Reject", "Failed"


def policy_should_press(door, command):
    state = door.state()
    if command == Cmd.Open:
        return state == State.Closed or (
            state == State.Stopped and door.direction_confirmed()
            and door.next_direction() == State.Opening)
    if command == Cmd.Close:
        return state == State.Open or (
            door.direction_confirmed() and state == State.Stopped
            and door.next_direction() == State.Closing)
    if command == Cmd.Stop:
        return not door.pending() and state in (State.Opening, State.Closing)
    return False


def policy_evaluate(door, command):
    """Returns (action, reason)."""
    if policy_should_press(door, command):
        return PRESS, None
    if command == Cmd.Stop and door.pending():
        return REJECT, "opener start or stop awaiting confirmation"
    state = door.state()
    if not door.moving() and not door.motor_running() and not door.pending():
        if command == Cmd.Open and state == State.Open:
            return NOOP, "already open"
        if command == Cmd.Close and state == State.Closed:
            return NOOP, "already closed"
        if command == Cmd.Stop:
            return NOOP, "already stopped"
    if door.startup_direction_unknown():
        return REJECT, "door position unknown after restart"
    if door.moving() or door.motor_running() or door.pending():
        return REJECT, "door movement in progress or awaiting confirmation"
    if not door.direction_confirmed():
        return REJECT, "door direction unconfirmed"
    return REJECT, "next press would move in opposite direction"


# ---------------------------------------------------------------------------
# DoorPositionRestore.h
# ---------------------------------------------------------------------------
class Reset(IntEnum):
    Software = 0
    Panic = 1
    InterruptWatchdog = 2
    TaskWatchdog = 3
    Watchdog = 4
    Usb = 5
    PowerOn = 6
    Brownout = 7
    Other = 8


RESET_TEXT = {
    Reset.PowerOn: "Power on", Reset.Software: "Software restart",
    Reset.Panic: "Crash (panic)", Reset.InterruptWatchdog: "Interrupt watchdog",
    Reset.TaskWatchdog: "Task watchdog", Reset.Watchdog: "Other watchdog",
    Reset.Brownout: "Brownout (power dip)", Reset.Usb: "USB reset", Reset.Other: "Unknown",
}


def restore_decide(reset, closed, present, position):
    if closed:
        return "ReedClosed"
    if reset in (Reset.PowerOn, Reset.Brownout, Reset.Other):
        return "ResetNotEligible"
    if not present:
        return "NoRecord"
    if not GarageDoorModel.valid_position(position):
        return "InvalidRecord"
    return "Restore"


# ---------------------------------------------------------------------------
# Persistent storage (stands in for ESP32 Preferences)
# ---------------------------------------------------------------------------
class Prefs:
    def __init__(self, path):
        self.path = path
        self.data = {}
        try:
            with open(path) as f:
                self.data = json.load(f)
        except (OSError, ValueError):
            self.data = {}

    def save(self):
        tmp = self.path + ".tmp"
        with open(tmp, "w") as f:
            json.dump(self.data, f, indent=1)
        os.replace(tmp, self.path)

    def get(self, key, default=None):
        return self.data.get(key, default)

    def put(self, key, value):
        if self.data.get(key) != value:
            self.data[key] = value
            self.save()

    def remove(self, key):
        if key in self.data:
            del self.data[key]
            self.save()


# ---------------------------------------------------------------------------
# Clock (simulated time may run faster than real time with --speed)
# ---------------------------------------------------------------------------
class Clock:
    def __init__(self, speed):
        self.speed = speed
        self.t0 = time.monotonic()

    def ms(self):
        return int((time.monotonic() - self.t0) * self.speed * 1000)


# ---------------------------------------------------------------------------
# EventLog (EventLog.cpp: 100 lines of 120 characters)
# ---------------------------------------------------------------------------
class EventLog:
    CAPACITY = 100
    TEXT_LEN = 120

    def __init__(self, millis, echo=True):
        self.millis = millis
        self.echo = echo
        self.entries = []
        self.next_seq = 1

    def log(self, text):
        text = text[: self.TEXT_LEN - 1]
        now = self.millis()
        self.entries.append((self.next_seq, now, text))
        self.next_seq += 1
        if len(self.entries) > self.CAPACITY:
            self.entries.pop(0)
        if self.echo:
            print("[%8d] %s" % (now, text), flush=True)

    def latest_seq(self):
        return self.next_seq - 1

    def since(self, since):
        return [e for e in self.entries if e[0] > since]


# ---------------------------------------------------------------------------
# The physical world: door, opener, reed switch
# ---------------------------------------------------------------------------
REED_CLOSED_BELOW = 0.02   # door within 2% of travel of the floor


class World:
    def __init__(self, args, clock):
        self.clock = clock
        self.open_time = args.open_time * 1000.0
        self.close_time = args.close_time * 1000.0
        self.lamp_mode = args.lamp_mode
        self.lamp_after_ms = args.lamp_after * 1000.0
        self.pos = {"closed": 0.0, "open": 1.0, "mid": 0.5}[args.start]
        self.motion = None             # None | "open" | "close"
        self.last_dir = None
        self.lamp_start = 0
        self.lamp_off_at = 0
        self.lamp_lit = False
        self.reed_override = None      # None | "closed" | "open"
        self.obstruction_enabled = False
        self.obstruction_pos = 0.5
        self.ignore_next_press = False
        self.wifi_up = True
        self.power = True
        self.prev_pb = False
        self.last_step = clock.ms()
        self.notes = []

    def note(self, text):
        self.notes.append((self.clock.ms(), text))
        del self.notes[:-30]
        print("      world: " + text, flush=True)

    def reed_closed(self):
        if self.reed_override == "closed":
            return True
        if self.reed_override == "open":
            return False
        return self.pos <= REED_CLOSED_BELOW

    def opener_press(self, source):
        """The opener's button logic: stop if moving, else run the other way."""
        now = self.clock.ms()
        if not self.power:
            return
        if self.ignore_next_press:
            self.ignore_next_press = False
            self.note("opener ignored the %s press (fault injection)" % source)
            return
        if self.motion:
            self.motion = None
            self.lamp_off_at = now + self.lamp_after_ms
            self.note("%s press: opener stopped at %d%%" % (source, round(self.pos * 100)))
            return
        if self.pos <= 0.0:
            direction = "open"
        elif self.pos >= 1.0:
            direction = "close"
        else:
            direction = "close" if self.last_dir == "open" else "open"
        self._run(direction)
        self.note("%s press: opener %s from %d%%" % (
            source, "opening" if direction == "open" else "closing", round(self.pos * 100)))

    def _run(self, direction):
        self.motion = direction
        self.last_dir = direction
        self.lamp_start = self.clock.ms()

    def set_position(self, fraction):
        self.motion = None
        self.pos = min(1.0, max(0.0, fraction))
        self.note("door moved by hand to %d%%" % round(self.pos * 100))

    def set_power(self, on):
        self.power = on
        if not on:
            self.motion = None
            self.lamp_lit = False
        self.note("power %s" % ("restored" if on else "lost"))

    def step(self, pb_high):
        now = self.clock.ms()
        dt = now - self.last_step
        self.last_step = now

        # PB input: act on the press at the start of the pulse
        if pb_high and not self.prev_pb:
            self.opener_press("controller")
        self.prev_pb = pb_high

        if self.motion and self.power:
            if self.motion == "open":
                self.pos += dt / self.open_time
                if self.pos >= 1.0:
                    self.pos = 1.0
                    self.motion = None
                    self.lamp_off_at = now + self.lamp_after_ms
                    self.note("door fully open")
            else:
                self.pos -= dt / self.close_time
                if (self.obstruction_enabled and self.pos <= self.obstruction_pos):
                    self.pos = self.obstruction_pos
                    self._run("open")
                    self.note("obstruction at %d%%: opener reversed" % round(self.pos * 100))
                elif self.pos <= 0.0:
                    self.pos = 0.0
                    self.motion = None
                    self.lamp_off_at = now + self.lamp_after_ms
                    self.note("door fully closed")

        # LAMP output
        if self.motion and self.power:
            if self.lamp_mode == "steady":
                self.lamp_lit = True
            else:                                 # flash: 500 ms on, 500 ms off
                self.lamp_lit = ((now - self.lamp_start) % 1000) < 500
        else:
            self.lamp_lit = self.power and now < self.lamp_off_at


# ---------------------------------------------------------------------------
# The controller firmware (main.cpp, StatusWebServer.cpp semantics)
# ---------------------------------------------------------------------------
class Firmware:
    def __init__(self, world, clock, prefs, reset, token, wifi_delay_ms, echo, host_ip):
        self.world = world
        self.clock = clock
        self.prefs = prefs
        self.token = token
        self.host_ip = host_ip
        self.reset = reset
        self.boot_ms0 = clock.ms()
        self.wifi_delay_ms = wifi_delay_ms
        self.log = EventLog(self.millis, echo)

        # DeviceStatus
        self.door_closed = False
        self.door_state = "Stopped"
        self.door_moving = False
        self.flash_moving = False
        self.obstruction = False
        self.control_pending = False
        self.control_enabled = False
        self.door_changes = 0
        self.last_change_ms = 0
        self.has_changed = False

        # main.cpp globals
        self.model = GarageDoorModel(OPENER_FLASH_FITTED)
        self.motion = MotionInput()
        self.trigger_active = False
        self.trigger_start = 0
        self.pb_pin_high = False
        self.pb_release_at = None
        self.position_storage_healthy = True
        self.position_restore_pending = False
        self.position_startup_settled = False
        self.flash_observed = False
        self.last_flash_at = 0
        self.had_ip = False
        self.ota_started = False
        self.time_logged = False
        self.server_running = False
        self.input_monitoring_started_at = 0
        self.synced_at = None

        # DebouncedInput
        self.reed_active = False
        self.reed_last_raw = False
        self.reed_last_change = 0

        self.setup()

    def millis(self):
        return self.clock.ms() - self.boot_ms0

    # ---- Preferences-backed state
    def boot_count(self):
        return self.prefs.get("bootCount", 0)

    def has_ip(self):
        return (self.wifi_ready_at is not None and self.millis() >= self.wifi_ready_at
                and self.world.wifi_up)

    # ---- DoorControlGate / control-enabled persistence
    def save_control_enabled(self, enabled):
        self.prefs.put("controlEnabled", 1 if enabled else 0)
        return True

    def restore_control_enabled(self):
        saved = self.prefs.get("controlEnabled")
        if saved is not None and saved > 1:
            self.log.log("Door control disabled: invalid saved setting")
            return
        self.control_enabled = (saved == 1)
        self.log.log("Door control %s" % ("enabled" if self.control_enabled else "disabled"))

    def set_control_enabled(self, enabled):
        if not enabled:
            self.pb_pin_high = False
            self.control_enabled = False
        saved = self.save_control_enabled(enabled)
        if not saved:
            self.control_enabled = False
            self.pb_pin_high = False
            self.log.log("Door control setting save failed; disabled until reboot, "
                         "previous setting may return")
            return False
        self.control_enabled = enabled
        self.log.log("Door control %s from web UI" % ("enabled" if enabled else "disabled"))
        return True

    # ---- door position storage
    def store_present(self):
        return self.prefs.get("position") is not None

    def store_record(self):
        p = self.prefs.get("position")
        return tuple(p) if p else (0, 0, 0, 0, 0)

    def clear_saved_position(self):
        self.position_restore_pending = False
        if not self.position_storage_healthy:
            return False
        self.prefs.remove("position")
        return True

    def save_settled_position(self):
        if (not self.position_storage_healthy or not self.position_startup_settled
                or not OPENER_FLASH_FITTED):
            return
        position = self.model.position()
        if position is None:
            self.clear_saved_position()
            return
        self.prefs.put("position", list(position))

    def load_saved_position(self):
        if not OPENER_FLASH_FITTED:
            self.log.log("Door position not restored: motion input not fitted")
            self.clear_saved_position()
            return
        decision = restore_decide(self.reset, self.door_closed, self.store_present(),
                                  self.store_record())
        if decision == "Restore":
            self.position_restore_pending = True
            return
        reason = {"ReedClosed": "reed closed", "ResetNotEligible": "reset reason",
                  "NoRecord": "no saved record"}.get(decision, "invalid record")
        self.log.log("Door position not restored: %s" % reason)
        self.clear_saved_position()

    # ---- opener button
    def press_door_internal(self):
        now = self.millis()
        if not self.control_enabled:
            self.log.log("Opener press rejected: door control disabled")
            return False
        if self.trigger_active or not self.model.can_press():
            self.log.log("Opener press rejected: busy")
            return False
        if not self.clear_saved_position():
            self.log.log("Opener press rejected: door position storage unavailable")
            return False
        self.pb_pin_high = True
        self.pb_release_at = now + OPENER_PULSE_MS
        self.model.press(now)
        self.trigger_active = True
        self.trigger_start = now
        self.control_pending = True
        self.log.log("Opener button pressed")
        return True

    def command_door(self, command):
        action, reason = policy_evaluate(self.model, command)
        if action != PRESS:
            return action, reason
        if self.press_door_internal():
            return PRESS, None
        return FAILED, "opener press failed"

    # ---- setup()
    def setup(self):
        self.wifi_ready_at = None
        self.pb_pin_high = False
        self.log.log("Device name: %s" % DEVICE_NAME)
        self.restore_control_enabled()
        count = self.boot_count() + 1
        self.prefs.put("bootCount", count)
        self.log.log("Boot #%d, last reset: %s, firmware %s" % (
            count, RESET_TEXT.get(self.reset, "Unknown"), FIRMWARE_VERSION))

        # door.begin()
        raw = self.world.reed_closed()
        self.reed_active = self.reed_last_raw = raw
        self.reed_last_change = self.millis()
        self.door_closed = self.reed_active
        self.model.begin(self.door_closed, self.millis())
        self.log.log("Initial door state: %s" % ("CLOSED" if self.door_closed else "OPEN"))
        self.load_saved_position()

        # setupNetwork(): joins Wi-Fi after wifi_delay_ms
        self.wifi_ready_at = self.millis() + 500 + self.wifi_delay_ms
        self.input_monitoring_started_at = self.millis()

    # ---- loop()
    def loop(self):
        now = self.millis()
        # esp_timer releasing the opener input
        if self.pb_release_at is not None and now >= self.pb_release_at:
            self.pb_pin_high = False
            self.pb_release_at = None
        if self.trigger_active and now - self.trigger_start >= OPENER_PULSE_MS:
            self.pb_pin_high = False
            self.trigger_active = False

        # Wi-Fi
        has_ip = self.has_ip()
        if has_ip and not self.had_ip:
            self.log.log("Wi-Fi connected to %s, %d dBm" % (SSID, RSSI))
            if not self.ota_started:
                self.ota_started = True
                self.log.log("Password-protected OTA updates ready")
            if not self.server_running:
                self.server_running = True
                self.log.log("Web UI started: http://%s/" % self.host_ip)
            if self.synced_at is None:
                self.synced_at = now + 1200
        if self.had_ip and not has_ip:
            self.log.log("Wi-Fi connection lost")
        self.had_ip = has_ip
        if not self.time_logged and self.synced_at is not None and now >= self.synced_at:
            self.time_logged = True
            self.log.log("Clock synchronised from network time")

        # door input (debounced)
        raw = self.world.reed_closed()
        if raw != self.reed_last_raw:
            self.reed_last_raw = raw
            self.reed_last_change = now
        if raw != self.reed_active and now - self.reed_last_change >= DEBOUNCE_MS:
            self.reed_active = raw
            self.door_closed = raw
            self.door_changes += 1
            self.last_change_ms = now
            self.has_changed = True
            self.log.log("Garage door %s" % ("CLOSED" if raw else "OPEN"))

        flash_lit = OPENER_FLASH_FITTED and self.world.lamp_lit
        reed_raw_closed = self.world.reed_closed()
        if flash_lit:
            self.flash_observed = True
            self.last_flash_at = now
        if flash_lit or self.door_closed or reed_raw_closed:
            if self.position_restore_pending and flash_lit:
                self.log.log("Door position not restored: motion detected")
            self.clear_saved_position()
        motor_running = OPENER_FLASH_FITTED and self.motion.update(flash_lit, now)
        previous_state = self.model.state()
        event = self.model.update(self.door_closed, motor_running, now)
        if event == Event.StartFailed:
            self.log.log("Opener did not start")
        if event == Event.Reversed:
            self.log.log("Opener reversed instead of stopping")
        if event == Event.SafetyTimeout:
            self.log.log("Opener movement exceeded safety limit")
        if previous_state != self.model.state():
            self.log.log("Door movement state: %s" % state_name(self.model.state()))
        if (not self.position_startup_settled
                and now - self.input_monitoring_started_at >= POSITION_SETTLE_MS):
            self.position_startup_settled = True
            if self.position_restore_pending:
                self.position_restore_pending = False
                if self.model.restore_position(self.store_record()):
                    self.log.log("Door position restored after restart")
                else:
                    self.log.log("Door position not restored: door state changed")
                    self.clear_saved_position()
        if (self.position_startup_settled and not reed_raw_closed
                and (not self.flash_observed or now - self.last_flash_at >= 1500
                     or motor_running)):
            self.save_settled_position()
        self.door_state = state_name(self.model.state())
        self.door_moving = motor_running if OPENER_FLASH_FITTED else self.model.moving()
        self.flash_moving = motor_running
        self.obstruction = self.model.obstruction()
        self.control_pending = (not self.position_storage_healthy or self.trigger_active
                                or not self.model.can_press())

    # ---- status JSON (StatusWebServer::handleStatus)
    def status_json(self):
        now = self.millis()
        synced = self.synced_at is not None and now >= self.synced_at
        has_ip = self.has_ip()
        return json.dumps({
            "doorClosed": self.door_closed,
            "doorState": self.door_state,
            "doorMoving": self.door_moving,
            "flashMoving": self.flash_moving,
            "obstruction": self.obstruction,
            "controlPending": self.control_pending,
            "controlEnabled": self.control_enabled,
            "doorChanges": self.door_changes,
            "lastChangeAgoS": (now - self.last_change_ms) // 1000 if self.has_changed else -1,
            "uptimeMs": now,
            "epoch": int(time.time()) if synced else 0,
            "wifi": has_ip,
            "ssid": SSID if has_ip else "",
            "rssi": RSSI if has_ip else 0,
            "deviceName": DEVICE_NAME,
            "ip": self.host_ip if has_ip else "",
            "bootCount": self.boot_count(),
            "resetReason": RESET_TEXT.get(self.reset, "Unknown"),
            "freeHeap": 187000 - (now // 1000) % 40 * 16,
            "minFreeHeap": 171000,
            "firmware": "%s (SIMULATOR %s)" % (FIRMWARE_VERSION, time.strftime("%b %d %Y %H:%M:%S")),
        }, separators=(",", ":"))

    def log_json(self, since):
        now = self.millis()
        synced = self.synced_at is not None and now >= self.synced_at
        entries = [{"s": s, "t": t, "m": m} for (s, t, m) in self.log.since(since)]
        return json.dumps({"latest": self.log.latest_seq(), "now": now,
                           "epoch": int(time.time()) if synced else 0, "entries": entries},
                          separators=(",", ":"))

    def reset_boot_count(self):
        self.prefs.put("bootCount", 0)
        self.log.log("Boot count reset from web UI")


# ---------------------------------------------------------------------------
# The simulator: world + firmware + a 10 ms loop
# ---------------------------------------------------------------------------
class Simulator:
    def __init__(self, args):
        self.args = args
        self.lock = threading.RLock()
        self.clock = Clock(args.speed)
        path = os.path.abspath(args.state)
        if args.fresh and os.path.exists(path):
            os.remove(path)
        self.prefs = Prefs(path)
        self.token = args.token or os.environ.get("GARAGE_CONTROL_TOKEN") or self.prefs.get("token")
        if not self.token:
            self.token = secrets.token_hex(32)
        self.prefs.put("token", self.token)
        if len(self.token) < 32:
            print("warning: token is shorter than the 32 characters the firmware requires")
        if args.enable_control:
            self.prefs.put("controlEnabled", 1)
        self.host_ip = guess_ip(args.host)
        self.world = World(args, self.clock)
        self.fw = None
        self.fw = self._boot(Reset.PowerOn)
        self.running = True

    def _boot(self, reset):
        return Firmware(self.world, self.clock, self.prefs, reset, self.token,
                        int(self.args.wifi_delay * 1000), not self.args.quiet, self.host_ip)

    def reboot(self, kind):
        reset = {"software": Reset.Software, "crash": Reset.Panic,
                 "watchdog": Reset.TaskWatchdog, "power": Reset.PowerOn}.get(kind, Reset.Software)
        with self.lock:
            self.world.note("controller reboot (%s)" % kind)
            self.fw = self._boot(reset)

    def power(self, on):
        with self.lock:
            self.world.set_power(on)
            if on:
                self.fw = self._boot(Reset.PowerOn)
            else:
                self.fw = None

    def tick(self):
        with self.lock:
            fw = self.fw
            self.world.step(fw.pb_pin_high if fw else False)
            if fw:
                fw.loop()

    def run(self):
        while self.running:
            self.tick()
            time.sleep(0.01)

    def physical_state(self):
        with self.lock:
            w, fw = self.world, self.fw
            return {
                "position": round(w.pos, 4),
                "motion": w.motion,
                "lamp": w.lamp_lit,
                "reedClosed": w.reed_closed(),
                "reedOverride": w.reed_override,
                "obstruction": w.obstruction_enabled,
                "obstructionPos": w.obstruction_pos,
                "ignoreNextPress": w.ignore_next_press,
                "wifiUp": w.wifi_up,
                "power": w.power,
                "pbHigh": bool(fw and fw.pb_pin_high),
                "openTime": w.open_time / 1000.0,
                "closeTime": w.close_time / 1000.0,
                "speed": self.clock.speed,
                "model": (fw.door_state if fw else "off"),
                "notes": [{"t": t, "m": m} for t, m in w.notes[-12:]],
                "token": self.token,
            }

    def action(self, req):
        a = req.get("action")
        v = req.get("value")
        with self.lock:
            w = self.world
            if a == "wall_button":
                w.opener_press("wall-button")
            elif a == "set_position":
                w.set_position(float(v))
            elif a == "obstruction":
                w.obstruction_enabled = bool(req.get("enabled"))
                if "position" in req:
                    w.obstruction_pos = min(0.95, max(0.05, float(req["position"])))
                w.note("obstruction %s at %d%%" % (
                    "on" if w.obstruction_enabled else "off", round(w.obstruction_pos * 100)))
            elif a == "reed":
                w.reed_override = v if v in ("closed", "open") else None
                w.note("reed switch %s" % (w.reed_override or "automatic"))
            elif a == "ignore_next_press":
                w.ignore_next_press = bool(v)
                w.note("opener will %signore the next press" % ("" if v else "not "))
            elif a == "wifi":
                w.wifi_up = bool(v)
                w.note("Wi-Fi %s" % ("up" if v else "down"))
            elif a == "power":
                pass
            elif a == "reboot":
                pass
            else:
                return False
        if a == "power":
            self.power(bool(v))
        elif a == "reboot":
            self.reboot(v or "software")
        return True


def guess_ip(host):
    if host not in ("0.0.0.0", ""):
        return host
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("192.0.2.1", 9))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except OSError:
        return "127.0.0.1"


# ---------------------------------------------------------------------------
# Web page
# ---------------------------------------------------------------------------
def load_page(path):
    candidates = [path] if path else [os.path.join(HERE, "StatusPage.h"),
                                       os.path.join(HERE, "status_page.html")]
    for p in candidates:
        if p and os.path.exists(p):
            text = open(p, encoding="utf-8").read()
            m = re.search(r'R"HTML\((.*)\)HTML"', text, re.S)
            return (m.group(1) if m else text).encode("utf-8"), p
    raise SystemExit("Cannot find the status page; pass --page StatusPage.h")


SIM_PAGE = r"""<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Garage Door Simulator</title>
<style>
:root{--bg:#f4f4f2;--card:#fff;--text:#1d1d1b;--muted:#6b6b66;--line:#ddddd8;--acc:#2878b5}
@media (prefers-color-scheme:dark){:root{--bg:#161615;--card:#22221f;--text:#ecece8;--muted:#a3a39c;--line:#3a3a36}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px/1.5 -apple-system,system-ui,sans-serif}
main{max-width:860px;margin:0 auto;padding:16px}h1{font-size:20px;margin:4px 0 4px}h2{font-size:15px;margin:0 0 8px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;margin-bottom:12px}
.sub{color:var(--muted);font-size:13px}.row{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin:6px 0}
button{font:inherit;font-size:13px;color:var(--text);background:var(--card);border:1px solid var(--line);border-radius:8px;padding:6px 10px;cursor:pointer}
button.on{background:var(--acc);color:#fff;border-color:var(--acc)}
.track{position:relative;height:150px;border:1px solid var(--line);border-radius:8px;background:linear-gradient(#cfe3f3,#eef5fb);overflow:hidden}
.door{position:absolute;left:10%;right:10%;bottom:0;background:#8a8a84;border:2px solid #555;border-bottom:0;transition:height .15s linear}
.floor{position:absolute;left:0;right:0;bottom:0;height:6px;background:#555}
.pill{display:inline-block;padding:2px 9px;border-radius:12px;font-size:13px;background:var(--line)}
.pill.y{background:#f2c06b;color:#3d2c10}.pill.g{background:#7fd3a6;color:#103a28}
code{background:var(--line);padding:1px 5px;border-radius:4px;word-break:break-all}
input[type=range]{width:200px}
</style></head><body><main>
<h1>Garage Door Simulator</h1>
<div class="sub">Physical side of the system. The controller's own page is <a href="/">/</a>.</div>
<div class="card"><h2>Door</h2>
<div class="track"><div class="door" id="door" style="height:0"></div><div class="floor"></div></div>
<div class="row"><span id="pos"></span><span id="motion" class="pill"></span><span id="lamp" class="pill"></span>
<span id="reed" class="pill"></span><span id="pb" class="pill"></span><span id="model" class="pill"></span></div>
<div class="row"><button onclick="act('wall_button')">Press wall button</button>
<label class="sub">Move by hand <input type="range" min="0" max="100" id="manual" onchange="act('set_position',this.value/100)"></label></div>
</div>
<div class="card"><h2>Faults</h2>
<div class="row"><button id="obs" onclick="toggleObs()">Obstruction while closing: off</button>
<label class="sub">at <input type="range" min="5" max="95" value="50" id="obspos" onchange="sendObs()"> <span id="obsv">50</span>%</label></div>
<div class="row"><button id="ign" onclick="toggleIgn()">Opener ignores next press: off</button>
<span class="sub">Reed:</span>
<button onclick="act('reed','auto')">auto</button><button onclick="act('reed','closed')">stuck closed</button><button onclick="act('reed','open')">stuck open</button></div>
<div class="row"><button id="wifi" onclick="toggleWifi()">Wi-Fi: up</button>
<button onclick="act('reboot','software')">Software reboot</button>
<button onclick="act('reboot','crash')">Crash reboot</button>
<button onclick="act('reboot','power')">Power-on reboot</button>
<button id="pow" onclick="togglePower()">Cut power</button></div>
<div class="sub">Software and crash reboots restore the saved door position if the door was open and still;
power-on reboots do not (see DoorPositionRestore).</div></div>
<div class="card"><h2>Simulator events</h2><div id="notes" class="sub"></div></div>
<div class="card"><h2>Control token</h2><code id="token"></code>
<div class="sub">Send it as <code>X-Garage-Token</code>. Open, close, stop and press also need <code>X-Garage-Control</code> and a
<code>text/plain</code> body equal to the command.</div></div>
</main><script>
let S={};
async function act(action,value,extra){await fetch('/sim/api/action',{method:'POST',body:JSON.stringify({action,value,...extra})});poll()}
function toggleObs(){S.obstruction=!S.obstruction;sendObs()}
function sendObs(){const p=$('obspos').value/100;$('obsv').textContent=$('obspos').value;act('obstruction',null,{enabled:S.obstruction,position:p})}
function toggleIgn(){act('ignore_next_press',!S.ignoreNextPress)}
function toggleWifi(){act('wifi',!S.wifiUp)}
function togglePower(){act('power',!S.power)}
const $=id=>document.getElementById(id);
function pill(id,text,y){const e=$(id);e.textContent=text;e.className='pill'+(y?' y':'')}
async function poll(){try{const s=await (await fetch('/sim/api/state',{cache:'no-store'})).json();S=s;
$('door').style.height=(s.position*100)+'%';$('pos').textContent='Door '+Math.round(s.position*100)+'% open';
pill('motion',s.motion?('Moving: '+s.motion):'Stopped',s.motion);pill('lamp',s.lamp?'LAMP lit':'LAMP off',s.lamp);
pill('reed',s.reedClosed?'Reed closed':'Reed open',!s.reedClosed);pill('pb',s.pbHigh?'Button input held':'Button input idle',s.pbHigh);
pill('model','Controller says: '+s.model,false);
$('obs').textContent='Obstruction while closing: '+(s.obstruction?'on':'off');$('obs').className=s.obstruction?'on':'';
$('ign').textContent='Opener ignores next press: '+(s.ignoreNextPress?'on':'off');$('ign').className=s.ignoreNextPress?'on':'';
$('wifi').textContent='Wi-Fi: '+(s.wifiUp?'up':'down');$('wifi').className=s.wifiUp?'':'on';
$('pow').textContent=s.power?'Cut power':'Restore power';$('pow').className=s.power?'':'on';
$('token').textContent=s.token;
$('notes').innerHTML=s.notes.slice().reverse().map(n=>(n.t/1000).toFixed(1)+'s  '+n.m).join('<br>')||'none yet'}catch(e){}}
setInterval(poll,500);poll();
</script></body></html>"""


# ---------------------------------------------------------------------------
# HTTP (StatusWebServer.cpp)
# ---------------------------------------------------------------------------
class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"
    sim = None
    page = b""

    # -- plumbing
    def log_message(self, fmt, *args):
        pass

    def _drop(self):
        """Device unreachable: close the connection without answering."""
        self.close_connection = True
        if not self.sim.args.quiet:
            print("      http: %s %s -> no response (device offline)" % (self.command, self.path),
                  flush=True)

    def _send(self, code, ctype, body, headers=None):
        data = body if isinstance(body, bytes) else body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        for k, v in (headers or {}).items():
            self.send_header(k, v)
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(data)
        if not self.sim.args.quiet and not self.path.startswith(("/api/status", "/api/log", "/sim")):
            print("      http: %s %s -> %d" % (self.command, self.path, code), flush=True)

    def _nostore(self, code, ctype, body):
        self._send(code, ctype, body, {"Cache-Control": "no-store"})

    def _online(self):
        fw = self.sim.fw
        return fw is not None and fw.server_running and fw.has_ip()

    # -- verbs
    def do_GET(self):
        url = urlsplit(self.path)
        path = url.path
        if path == "/sim":
            return self._nostore(200, "text/html", SIM_PAGE)
        if path == "/sim/api/state":
            return self._nostore(200, "application/json", json.dumps(self.sim.physical_state()))
        with self.sim.lock:
            if not self._online():
                return self._drop()
            fw = self.sim.fw
            if path == "/":
                return self._nostore(200, "text/html", self.page)
            if path == "/api/status":
                return self._nostore(200, "application/json", fw.status_json())
            if path == "/api/log":
                since = 0
                q = parse_qs(url.query)
                if "since" in q:
                    m = re.match(r"\s*(\d+)", q["since"][0])
                    since = int(m.group(1)) if m else 0
                return self._nostore(200, "application/json", fw.log_json(since))
        self._send(404, "text/plain", "Not found")

    def do_POST(self):
        url = urlsplit(self.path)
        path = url.path
        length = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(length) if length else b""
        if path == "/sim/api/action":
            try:
                ok = self.sim.action(json.loads(raw or b"{}"))
            except (ValueError, TypeError):
                ok = False
            return self._nostore(200 if ok else 400, "application/json",
                                 '{"ok":true}' if ok else '{"ok":false}')
        # ESP32 WebServer: a form-encoded body is parsed into arguments, anything
        # else is exposed as the single argument "plain".
        ctype = (self.headers.get("Content-Type") or "").lower()
        plain = "" if ctype.startswith(("application/x-www-form-urlencoded",
                                         "multipart/form-data")) else raw.decode("latin-1")
        routes = {
            "/api/reset-boot-count": self._reset_boot_count,
            "/api/door/press": self._door_press,
            "/api/door/open": lambda fw, pl: self._door_command(fw, pl, Cmd.Open, "open"),
            "/api/door/close": lambda fw, pl: self._door_command(fw, pl, Cmd.Close, "close"),
            "/api/door/stop": lambda fw, pl: self._door_command(fw, pl, Cmd.Stop, "stop"),
            "/api/door/control": self._control_enabled,
        }
        with self.sim.lock:
            if not self._online():
                return self._drop()
            fw = self.sim.fw
            handler = routes.get(path)
            if handler is None:
                return self._send(404, "text/plain", "Not found")
            handler(fw, plain)

    # -- handlers
    def _authorize(self, fw):
        token = fw.token
        if token and (self.headers.get("X-Garage-Token") or "") == token:
            return True
        self._nostore(401, "text/plain", "Invalid garage control token")
        return False

    def _header(self, name):
        return self.headers.get(name) or ""

    def _reset_boot_count(self, fw, plain):
        if not self._authorize(fw):
            return
        if self._header("X-Garage-Control") != "reset":
            return self._send(400, "text/plain", "Invalid request")
        fw.reset_boot_count()
        self._nostore(200, "application/json", '{"ok":true}')

    def _door_press(self, fw, plain):
        if not self._authorize(fw):
            return
        if self._header("X-Garage-Control") != "press" or plain != "press":
            self._nostore(400, "text/plain", "Invalid request")
        elif not fw.control_enabled:
            self._nostore(403, "text/plain", "Door control is disabled")
        elif not fw.press_door_internal():
            self._nostore(409, "text/plain", "Opener is busy")
        else:
            self._nostore(200, "application/json", '{"ok":true}')

    def _door_command(self, fw, plain, command, token):
        if not self._authorize(fw):
            return
        if self._header("X-Garage-Control") != token or plain != token:
            self._nostore(400, "text/plain", "Invalid request")
        elif not fw.control_enabled:
            self._nostore(403, "text/plain", "Door control is disabled")
        else:
            action, reason = fw.command_door(command)
            if action == FAILED:
                self._nostore(409, "text/plain", "Opener is busy")
            elif action == PRESS:
                self._nostore(200, "application/json", '{"ok":true,"pressed":true}')
            else:
                ok = "true" if action == NOOP else "false"
                self._nostore(200, "application/json", '{"ok":%s,"pressed":false,"reason":%s}' % (
                    ok, json.dumps(reason)))

    def _control_enabled(self, fw, plain):
        if not self._authorize(fw):
            return
        if self._header("X-Garage-Control") != "control" or plain not in ("enable", "disable"):
            self._nostore(400, "text/plain", "Invalid request")
        elif not fw.set_control_enabled(plain == "enable"):
            self._nostore(500, "text/plain", "Could not save setting")
        else:
            self._nostore(200, "application/json", '{"ok":true}')


# ---------------------------------------------------------------------------
def parse_args(argv=None):
    p = argparse.ArgumentParser(description="Garage Door Controller simulator (firmware 4.3.0)")
    p.add_argument("--host", default="0.0.0.0", help="address to listen on (default 0.0.0.0)")
    p.add_argument("--port", type=int, default=8080, help="port (default 8080; the controller uses 80)")
    p.add_argument("--token", help="control token (default: saved random token)")
    p.add_argument("--enable-control", action="store_true", help="start with door control enabled")
    p.add_argument("--start", choices=["closed", "open", "mid"], default="closed",
                   help="initial door position (default closed)")
    p.add_argument("--open-time", type=float, default=14.0, help="seconds to open (default 14)")
    p.add_argument("--close-time", type=float, default=13.0, help="seconds to close (default 13)")
    p.add_argument("--lamp-mode", choices=["flash", "steady"], default="flash",
                   help="how the opener drives LAMP while moving (default flash)")
    p.add_argument("--lamp-after", type=float, default=0.0,
                   help="seconds LAMP stays lit after the motor stops (default 0)")
    p.add_argument("--wifi-delay", type=float, default=2.0, help="seconds to join Wi-Fi after boot")
    p.add_argument("--speed", type=float, default=1.0, help="run time faster than real time")
    p.add_argument("--state", default=os.path.join(HERE, "garage_sim_state.json"),
                   help="file standing in for the controller's saved settings")
    p.add_argument("--fresh", action="store_true", help="delete saved state at start")
    p.add_argument("--page", help="StatusPage.h or an .html file to serve at /")
    p.add_argument("--quiet", action="store_true", help="do not print the log")
    return p.parse_args(argv)


def serve(args):
    sim = Simulator(args)
    Handler.sim = sim
    Handler.page, page_path = load_page(args.page)
    threading.Thread(target=sim.run, daemon=True).start()
    server = ThreadingHTTPServer((args.host, args.port), Handler)
    server.daemon_threads = True
    shown = "localhost" if args.host in ("0.0.0.0", "") else args.host
    print("Garage Door Simulator (firmware %s behaviour)" % FIRMWARE_VERSION)
    print("  controller page : http://%s:%d/   (%s)" % (shown, args.port, os.path.basename(page_path)))
    print("  simulator panel : http://%s:%d/sim" % (shown, args.port))
    if args.host in ("0.0.0.0", ""):
        print("  from your phone : http://%s:%d/" % (sim.host_ip, args.port))
    print("  control token   : %s" % sim.token)
    print("  control         : %s (enable it on the page, or start with --enable-control)" %
          ("enabled" if sim.prefs.get("controlEnabled") == 1 else "disabled"))
    print("Ctrl-C to stop.\n", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        sim.running = False
        server.server_close()


if __name__ == "__main__":
    serve(parse_args())
