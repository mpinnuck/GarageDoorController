# Garage Door Controller

Firmware for a Seeed XIAO ESP32C3 garage-door controller, operated through its
web page or HTTP endpoints.

## Setup

Copy `include/secrets.example.h` to `include/secrets.h` and set the Wi-Fi
credentials, OTA password and a unique random control token of at least 32
characters. Generate a token with `openssl rand -hex 32`. Keep
`include/secrets.h` and compiled firmware private. When uploading an OTA update,
set the `OTA_PASSWORD` environment variable to the same password:

```sh
read -s OTA_PASSWORD
export OTA_PASSWORD
pio run -e ota -t upload
```

For USB installation or recovery:

```sh
pio run -e usb -t upload
pio device monitor
```

The firmware uses two OTA app slots in `min_spiffs.csv`. If the board still has
a different partition layout, erase it over USB before the first upload:

```sh
pio run -e usb -t erase
pio run -e usb -t upload
```

## Web UI and HTTP API

Open `http://garagecontroller.local/` or the controller's IP address. The page
shows door and motion status, Wi-Fi information, boot details, an event log,
and controls. The device advertises the Wi-Fi hostname `garagecontroller`.

Enter the control token in the page's password field to use its buttons. The
page saves it in browser local storage so it persists between sessions; clear
the field to remove it. A rejected token is also removed. Storage is separate
for each browser and address, and browser settings or private browsing may
prevent persistence. The token is stored unencrypted and accessible to scripts
on the same origin and anyone with access to the browser profile; do not use
this feature on shared devices. The page also sends the token over unencrypted
HTTP and is not for untrusted networks.

Door commands are `POST` requests with a plain-text body, matching
`X-Garage-Control` header and `X-Garage-Token` set to the control token:

| Endpoint | Header and body | Behavior |
| --- | --- | --- |
| `/api/door/press` | `press` | Raw press of the opener input |
| `/api/door/open` | `open` | Press only if the model indicates the door can open |
| `/api/door/close` | `close` | Press only if the model indicates the door can close |
| `/api/door/stop` | `stop` | Press while the model indicates movement, unless a start or stop is awaiting confirmation |
| `/api/door/control` | `enable` or `disable` | Enable or disable controller commands; header is `control` |

For example:

```sh
read -s GARAGE_TOKEN
curl -X POST http://garagecontroller.local/api/door/open \
  -H 'X-Garage-Control: open' \
  -H "X-Garage-Token: $GARAGE_TOKEN" \
  -H 'Content-Type: text/plain' \
  --data 'open'
```

For an Apple Shortcut's Get Contents of URL action, use POST, set
`X-Garage-Control` and the request body to the command, and set
`X-Garage-Token` to the token from `include/secrets.h`. Keep the shortcut
private. For boot-count reset, use `POST /api/reset-boot-count` with
`X-Garage-Control: reset` and the same token.

The conditional endpoints return `{"ok":true,"pressed":true}` when they press.
A successful no-op returns, for example,
`{"ok":true,"pressed":false,"reason":"already closed"}`. A refused command
returns HTTP 200 with `ok:false`, `pressed:false`, and a reason, for example
`"door direction unconfirmed"`.
Missing or incorrect tokens return HTTP 401. Invalid requests, disabled
control and failed presses return HTTP 400, 403 and 409 respectively.

After a power-on with the reed open, or another restart without a usable saved
position, the controller reports Open with direction unconfirmed. Open is a
no-op; Close is allowed and predicts closing. The motor indicator confirms a
start, not its direction, and the reed confirms closure. If the predicted
closing movement ends without the reed closing, direction is unconfirmed and
conditional Open and Close commands are refused. To recover, use the wall
button or remote to close the door until the reed confirms it is closed,
checking the door's path and direction. If the motor does not start, the
controller returns to the assumed Open state and Close can be retried.
This assumes an open reed means the door is open; if the door was left part-way
through travel during a power outage, the first Close press may move it farther
open. Check the door's path and use the wall button or remote if needed. The
raw web press bypasses the conditional direction checks.
Status and utility endpoints are `GET /api/status`, `GET /api/log?since=N`,
and `POST /api/reset-boot-count`.

The token restricts control requests, but HTTP does not encrypt it: anyone able
to observe traffic can capture and replay it. Status and logs are public.
Restrict network access and do not expose the device directly to the internet.
If the token is exposed, generate a new one, update the firmware and replace
it in all shortcuts.

## Hardware and behavior

- D1 (GPIO3): door reed input; LOW means the door is closed.
- D2 (GPIO4): active-HIGH opener input, pulsed for 500 ms.
- D3 (GPIO5): active-LOW motor indicator input through the PC817.
- The web button performs a raw opener press. Its Open/Stop/Close label is an
  estimate because the opener controls its own cycle.
- Conditional HTTP commands use the controller's door model and are refused
  when a requested action is not safe to infer. Open or Close from Stopped
  requires a confirmed direction; Close from Open is allowed without one.
  Without the motor indicator, the model cannot confirm direction, so commands
  from Stopped will not press; the raw press remains available.
- The control lock is saved across restarts and defaults to disabled on first
  use. Disabling it releases an active pulse but does not stop a moving door or
  disable the opener's physical wall button or remote.
- The reed measures only the closed position. Open position, direction and
  obstruction status are estimates. Motor-indicator behavior must be verified
  on the actual opener before relying on movement tracking.

The controller is not a physical safety interlock. Verify the opener's safety
devices, keep the door path clear, and check the door before operating it.

## Tests

Run the host-side tests from this directory:

```sh
c++ -std=c++11 -Iinclude src/GarageDoorModel.cpp test/test_garage_door_model.cpp -o /tmp/garage-door-model-test
/tmp/garage-door-model-test
c++ -std=c++11 -Itest/fakes/http -Iinclude src/GarageDoorModel.cpp src/StatusWebServer.cpp test/test_status_web_server.cpp -o /tmp/status-web-server-test
/tmp/status-web-server-test
c++ -std=c++11 -Itest/fakes -Iinclude src/GarageDoorModel.cpp src/DoorPositionStore.cpp test/test_door_position_store.cpp -o /tmp/door-position-store-test
/tmp/door-position-store-test
node test/test_status_page.js
c++ -std=c++11 -Iinclude test/test_door_control_gate.cpp -o /tmp/door-control-gate-test
/tmp/door-control-gate-test
```
