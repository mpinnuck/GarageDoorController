# Garage Door Controller

Firmware for a Seeed XIAO ESP32C3 garage-door controller. The controller is
operated through its web page or HTTP endpoints; it does not use HomeKit or
Matter.

## Setup

Copy `include/secrets.example.h` to `include/secrets.h` and set the Wi-Fi
credentials and OTA password. Keep `include/secrets.h` private. When uploading
an OTA update, set the `OTA_PASSWORD` environment variable to the same password:

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

Door commands are `POST` requests with a plain-text body and matching
`X-Garage-Control` header:

| Endpoint | Header and body | Behavior |
| --- | --- | --- |
| `/api/door/press` | `press` | Raw press of the opener input |
| `/api/door/open` | `open` | Press only if the model indicates the door can open |
| `/api/door/close` | `close` | Press only if the model indicates the door can close |
| `/api/door/stop` | `stop` | Press only while the model indicates movement |
| `/api/door/control` | `enable` or `disable` | Enable or disable controller commands; header is `control` |

For example:

```sh
curl -X POST http://garagecontroller.local/api/door/open \
  -H 'X-Garage-Control: open' \
  -H 'Content-Type: text/plain' \
  --data 'open'
```

The command endpoints return `{"ok":true,"pressed":false}` when a conditional
command is a no-op. Status and utility endpoints are `GET /api/status`,
`GET /api/log?since=N`, and `POST /api/reset-boot-count`.

The web interface and API have no authentication. The matching command header
is a request check, not an access-control mechanism. Anyone who can reach the
device on the network can operate the door or change its control setting;
restrict network access accordingly.

## Hardware and behavior

- D1 (GPIO3): door reed input; LOW means the door is closed.
- D2 (GPIO4): active-HIGH opener input, pulsed for 500 ms.
- D3 (GPIO5): active-LOW motor indicator input through the PC817.
- The web button performs a raw opener press. Its Open/Stop/Close label is an
  estimate because the opener controls its own cycle.
- Conditional HTTP commands use the controller's door model and may be
  successful no-ops when a requested action is not safe to infer.
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
c++ -std=c++11 -Itest/fakes -Iinclude src/GarageDoorModel.cpp src/DoorPositionStore.cpp test/test_door_position_store.cpp -o /tmp/door-position-store-test
/tmp/door-position-store-test
node test/test_status_page.js
c++ -std=c++11 -Iinclude test/test_door_control_gate.cpp -o /tmp/door-control-gate-test
/tmp/door-control-gate-test
```
