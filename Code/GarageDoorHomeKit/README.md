# GarageDoorHomeKit

Apple HomeKit contact controller firmware for the garage door controller, using
[HomeSpan](https://github.com/HomeSpan/HomeSpan). Replaces `../GarageDoorMatter`
(kept unchanged). Seeed XIAO ESP32C3, door reed on D1 (GPIO3), reed closed = door shut.

- Apple Home shows the reed contact and a **Door Moving** motion sensor, plus a
  momentary **Open Stop Close** switch for the opener's wall-button input.
- Same web page as before at `http://garagecontroller.local/` (or the reserved IP);
  the device advertises the Wi-Fi hostname `garagecontroller` (the router may show it
  with its own local DNS suffix, such as `.lan`)
- **Firmware updates over Wi-Fi** once installed

HomeKit only: Google Home can no longer see the controller.

## Setup
1. Copy `include/secrets.example.h` to `include/secrets.h` and fill in the Wi-Fi
   password, an 8-digit setup code and an OTA password. Put the same OTA password in
   `platformio.ini` under `[env:ota]` (`upload_flags = --auth=...`).
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

## Updates over Wi-Fi
```sh
pio run -e ota -t upload
```
The Mac must be on the home network. If an update fails to start, HomeSpan rolls back
to the previous firmware.

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
reason, HomeKit pairing and setup code, and a live event log. The button and the
HomeKit switch both pulse the same opener input: each press advances the
opener's own open/stop/close cycle, rather than commanding a guaranteed direction.
The web button suggests **Stop** while moving, **Open** when the reed is closed,
and **Close** otherwise. This label is only a guess: a partly open or stopped
door may move in the opposite direction when pressed.
The web interface has no authentication; anyone who can reach the device on the
network can operate the door. Restrict network access accordingly.
The device hostname is logged at startup and displayed above the IP address.

The **Control enabled: Disable / Control disabled: Enable** button locks or unlocks both the web
opener button and the HomeKit opener switch. When locked, the firmware rejects
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
positions; a door that is not closed at startup is reported as stopped.

The model uses 15 seconds for opening and 13.5 seconds for closing. With FLASH,
an unconfirmed start times out after 3 seconds, and movement tracking stops
after 30 seconds. If FLASH is not fitted, add `-DOPENER_FLASH_FITTED=0` to
`build_flags` in `platformio.ini`; motion and end states are then estimates
based on those travel times and the reed. External movement can only be detected
when the reed opens; a remote press while already open cannot be detected in
this mode.

Test the logic on a host with:

```sh
c++ -std=c++11 -Iinclude src/GarageDoorModel.cpp test/test_garage_door_model.cpp -o /tmp/garage-door-model-test
/tmp/garage-door-model-test
node test/test_status_page.js
c++ -std=c++11 -Iinclude test/test_door_control_gate.cpp -o /tmp/door-control-gate-test
/tmp/door-control-gate-test
```

Source files:
- `main.cpp`          HomeSpan setup, door input, BOOT reset
- `HomeKitDoorController` HomeKit Contact Controller service
- `DebouncedInput`    reed switch with 500 ms debounce
- `EventLog`          ring buffer of log lines, also mirrored to Serial
- `StatusWebServer`   port 80: `/`, `/api/status`, `/api/log?since=N`, `POST /api/door/press`, `POST /api/reset-boot-count`
- `StatusPage.h`      the HTML/JS page
- `ClockSync`         NTP time (Sydney time zone) for log timestamps
- `NetworkStatus`     IP, SSID and signal from the Wi-Fi interface
- `BootInfo`          persistent boot counter and last reset reason
- `DeviceStatus`      door state shared with the web server
- `GarageDoorModel`   FLASH filtering and estimated opener state
