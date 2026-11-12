# GarageDoorHomeKit

Apple HomeKit garage door controller firmware, using
[HomeSpan](https://github.com/HomeSpan/HomeSpan). Replaces `../GarageDoorMatter`
(kept unchanged). Seeed XIAO ESP32C3, door reed on D1 (GPIO3), reed closed = door shut.

- Apple Home shows a garage door opener, the reed contact and a **Door Moving**
  motion sensor. There is no separate HomeKit wall-button switch.
- Same web page as before at `http://garagecontroller.local/` (or the reserved IP);
  the device advertises the Wi-Fi hostname `garagecontroller` (the router may show it
  with its own local DNS suffix, such as `.lan`)
- **Firmware updates over Wi-Fi** once installed

HomeKit only: Google Home can no longer see the controller.

## Setup
1. Copy `include/secrets.example.h` to `include/secrets.h` and fill in the Wi-Fi
   password, an 8-digit setup code and an OTA password. Set the `OTA_PASSWORD`
   environment variable to the same OTA password when uploading.
2. In the Home app, remove the old Matter "Garage Door" accessory.

## First install (USB, once)
The partition layout changes from Matter's single 3 MB slot to two 1.9 MB slots,
which only happens over USB.

```sh
pio run -e usb -t erase
pio run -e usb -t upload
pio device monitor
```

If the board is not detected, hold BOOT while plugging in USB.

## Add to Apple Home
Home app > **+** > Add Accessory > More options... > **Garage Door** > enter the setup code.
Accept the "Uncertified Accessory" warning (normal for DIY HomeKit devices).
After upgrading from the switch-based firmware, check that the garage door service
appears and update any scenes or automations that used the old switch. If Home
does not refresh the accessory, remove and add it again.

## Updates over Wi-Fi
In the terminal used for the upload, enter the current device OTA password
when prompted (it will not be displayed):

```sh
read -s OTA_PASSWORD
export OTA_PASSWORD
```

Then run:

```sh
pio run -e ota -t upload
```
The Mac must be on the home network. If an update fails to start, HomeSpan rolls back
to the previous firmware.
If changing the password, authenticate the update with the current device
password; the new password in `include/secrets.h` takes effect after the update.

When installing the firmware that first changes the hostname to `garagecontroller`,
upload over USB or target the controller's current IP address explicitly:

```sh
pio run -e ota -t upload --upload-port <controller-IP>
```

Do not use `garagedoor.local` if it belongs to another device.

## Reset
Hold BOOT for 5 s: forgets the HomeKit pairing and restarts, ready to add again.

## Door control and Web UI
The web page includes an opener button and a moving indicator,
as well as separate reed sensor (Open/Closed) and estimated control states,
Wi-Fi signal, uptime, boot count (with Reset), last reset
reason, HomeKit pairing and setup code, and a live event log. The web button
pulses the opener input: each press advances the opener's own open/stop/close
cycle, rather than commanding a guaranteed direction.
The web button suggests **Stop** while moving, **Open** when the reed is closed,
and **Close** otherwise. This label is only a guess: a partly open or stopped
door may move in the opposite direction when pressed.
The API also provides `POST /api/door/open`, `/api/door/close`, and
`/api/door/stop`. Open presses only when the door is closed or stopped after
closing; Close presses only when open or stopped after opening; Stop presses
only while the model reports opening or closing. Other states are successful
no-ops (`{"ok":true,"pressed":false}`). These requests use the matching
`X-Garage-Control` header and request body (`open`, `close`, or `stop`), like
the raw `/api/door/press` endpoint uses `press`.
The web interface has no authentication; anyone who can reach the device on the
network can operate the door. Restrict network access accordingly.
The device hostname is logged at startup and displayed above the IP address.

The **Control enabled: Disable / Control disabled: Enable** button locks or unlocks both the web
opener button and HomeKit door control. When locked, the firmware rejects
opener commands before energizing PB; the reed and motion sensors still work.
The setting is saved across restarts and defaults to disabled on first use or
if it cannot be read. If saving fails, the firmware disables control for the
current session and reports an error; the previously saved setting may return
after a restart. Disabling releases an active PB pulse but does not stop a door
already moving or disable the opener's physical wall button or remote.
This is a software lock, not a physical safety interlock. Anyone with web access
can re-enable it.

D2 (GPIO4) drives the opener input high for 500 ms. D3 (GPIO5) reads FLASH
through the PC817, active low. FLASH pulses shorter than 50 ms are ignored and
motion remains active until FLASH has been off for 1.5 seconds. The circuit
notes require measuring FLASH on the actual opener: confirm it reflects movement
and that its off gaps are shorter than 1.5 seconds before relying on the
indicator. The closed reed on D1 always takes priority for the displayed door
state. The open position and obstruction warning are estimates, not measured
positions; a door that is not closed at startup is initially reported as stopped.

The model uses 15 seconds for opening and 13.5 seconds for closing. With FLASH,
an opening that stops after at least 13 seconds is estimated as fully open,
giving a 2-second margin on the measured 15-second travel. A commanded stop
is still reported as stopped. An unconfirmed start times out after 3 seconds,
and movement tracking stops after 30 seconds. If FLASH is not fitted, add `-DOPENER_FLASH_FITTED=0` to
`build_flags` in `platformio.ini`; motion and end states are then estimates
based on those travel times and the reed. External movement can only be detected
when the reed opens; a remote press while already open cannot be detected in
this mode.

## Safety

The controller is not a physical safety interlock. Verify the opener's own
safety devices and keep the door's path clear. The reed measures only the
closed position; the open position, direction and obstruction warning are
estimates. LAMP indicates motor activity, not direction. Confirm on the actual
opener that LAMP reflects movement and its off gaps are shorter than 1.5 seconds.
The controller cannot guarantee a requested direction or detect every obstruction.
An uncommanded partial stop at or after 13 seconds can be mistaken for fully
open, which could make a subsequent Close press move the door the wrong way.
Measure how long LAMP stays on after the motor stops: delayed shutoff can distort
the opening estimate, and a courtesy light that remains on may cause a safety
timeout. Do not rely on LAMP-based tracking until its behavior is verified.

The HomeKit garage door service replaces the plain opener switch, but the web
button remains a raw press. The web interface is unauthenticated: restrict
network access. Disabling control rejects HomeKit commands and web presses,
but does not stop a moving door or disable the wall button or remote. Contact
and motion sensors continue to report.

HomeKit target requests make at most one press. Requests for an already reached
or confirmed moving direction do nothing; requests requiring an uncertain or
opposite direction are rejected. After a confirmed partial stop, a request
can press only in the model's inferred next direction. LAMP confirms motor
activity, not direction; direction remains an estimate based on the opener's
cycle. A closing movement that stops without the reed closing is reported as
stopped with estimated obstruction, and HomeKit rejects both directions until
the reed closes or direction becomes known again. An opening movement that
stops before its estimated completion without a commanded stop also leaves
direction unknown.

Rejected HomeKit requests are recorded in the web event log with the requested
direction and the reason, such as disabled control, a storage fault, an
opposite direction or unconfirmed movement. If multiple conditions apply, the
log reports the first one checked.

### Restart behavior

With LAMP fitted, the controller saves settled open-reed states and restores a
valid record after a software (including OTA), crash, watchdog or USB reset.
It clears the record when the reed closes, when LAMP activity is detected and
before a controller press. Power-on, brownout and other reset reasons do not
restore the record. A closed reed always takes priority.

HomeKit waits at least 1.5 seconds after input monitoring begins for LAMP to
settle. The table describes behavior after that initial period, assuming
control is enabled, storage is healthy and the opener is idle unless noted.
“Reed open” means only that the door is **not fully closed**, not that it is
fully open.

| Restart and door state | HomeKit state | HomeKit Close | HomeKit Open |
| --- | --- | --- | --- |
| Any reset, reed closed, motor idle | Closed (measured) | No press | One press |
| Eligible reset, reed open, saved Open | Open (estimated) | One press | No press |
| Eligible reset, reed open, saved Stopped after a commanded stop while opening | Stopped | One press | Rejected |
| Eligible reset, reed open, saved Stopped after a commanded stop while closing | Stopped | Rejected | One press |
| Eligible reset, reed open, saved Stopped with unconfirmed direction | Stopped | Rejected | Rejected |
| Eligible reset, reed open, no saved record, such as a detected mid-movement reset | Stopped | One assumed press | Rejected |
| Power-on, brownout or another ineligible reset, reed open | Stopped | One assumed press | Rejected |
| Any reset, reed open and movement detected during startup | Stopped (direction unknown) | Rejected while moving | Rejected while moving |

A saved unconfirmed state can result from a reversal, safety timeout or
uncommanded stop. It remains uncertain after an eligible restart; it does not
fall back to another assumed press. If startup movement is detected, the saved
record is cleared. When movement stops, a closed reed establishes Closed;
otherwise the door remains Stopped and both directions are rejected.

**Assumed Close:** When there is no usable record, Close can make one press on
the assumption that the next press closes. The log says
`HomeKit close: assuming next press closes after startup`. HomeKit reports
Stopped while it moves because the direction is not confirmed.

- If the reed closes, the door becomes Closed and normal control resumes.
- If the reed remains open when movement stops, the door remains Stopped with
  unknown direction and both HomeKit directions are rejected. Close it with
  the wall button, remote or web button to regain a known position.
- If LAMP does not confirm a start within three seconds, the failure is logged
  and the target reverts. The original HomeKit write may already have succeeded;
  another assumed request can be attempted.

This lockout is not permanent across all restarts: a power-on, brownout or
restart with no usable record can permit another assumed press. A saved
position can also become stale if the remote or wall button is used while the
controller is restarting, before LAMP is observed, or if LAMP fails to
indicate movement reliably. Even a movement still in progress at startup is
only detected if a qualifying LAMP signal is observed. Do not rely on the
restart logic as a physical safety guarantee; check the door before operating it.

Position-storage errors are logged and disable controller presses for the
current session; a failed invalidation may leave a stale record for a later
restart. Without LAMP, saved positions are not restored and HomeKit rejects
requests requiring movement; the web button remains available as a raw press.

Test the logic on a host with:

```sh
c++ -std=c++11 -Iinclude src/GarageDoorModel.cpp test/test_garage_door_model.cpp -o /tmp/garage-door-model-test
/tmp/garage-door-model-test
c++ -std=c++11 -Itest/fakes -Iinclude src/GarageDoorModel.cpp src/HomeKitDoorController.cpp test/test_homekit_door.cpp -o /tmp/homekit-door-test
/tmp/homekit-door-test
c++ -std=c++11 -Itest/fakes -Iinclude src/GarageDoorModel.cpp src/DoorPositionStore.cpp test/test_door_position_store.cpp -o /tmp/door-position-store-test
/tmp/door-position-store-test
node test/test_status_page.js
c++ -std=c++11 -Iinclude test/test_door_control_gate.cpp -o /tmp/door-control-gate-test
/tmp/door-control-gate-test
```

Source files:
- `main.cpp`          HomeSpan setup, door input, BOOT reset
- `HomeKitDoorController` HomeKit garage door, contact and motion services
- `DebouncedInput`    reed switch with 500 ms debounce
- `EventLog`          ring buffer of log lines, also mirrored to Serial
- `StatusWebServer`   port 80: `/`, `/api/status`, `/api/log?since=N`, `POST /api/door/{press,open,close,stop}`, `POST /api/reset-boot-count`
- `StatusPage.h`      the HTML/JS page
- `ClockSync`         NTP time (Sydney time zone) for log timestamps
- `NetworkStatus`     IP, SSID and signal from the Wi-Fi interface
- `BootInfo`          persistent boot counter and last reset reason
- `DeviceStatus`      door state shared with the web server
- `GarageDoorModel`   FLASH filtering and estimated opener state
