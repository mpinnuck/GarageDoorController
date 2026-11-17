# Garage Door Simulator (firmware 4.3.0)

Runs the controller's own web page and HTTP API on your computer, driven by a
simulated opener, door and reed switch, so you can develop iOS Shortcuts, curl
scripts and the web UI without the real controller or door. Python 3, standard
library only.

```sh
python3 garage_door_simulator.py --enable-control
```

* `http://localhost:8080/`    the controller's page (the same HTML as StatusPage.h)
* `http://localhost:8080/sim` the physical side: wall button, obstruction, reed fault, reboots, Wi-Fi/power loss
* The control token is printed at start and kept in `garage_sim_state.json` (override with `--token`).
* From the phone use `http://<computer IP>:8080/...` (printed at start). Real device: port 80, host `garagecontroller.local`.

## What is real and what is simulated

The firmware logic is a line-for-line Python port of 4.3.0: `DebouncedInput` (500 ms),
`MotionInput`, `GarageDoorModel` (including unsigned 32-bit time arithmetic and the short-run fix in `update()`),
`DoorWebCommandPolicy`, `DoorControlGate`, `DoorPositionRestore` and the main loop.
`StatusWebServer` is reproduced for status codes, headers, JSON and the 401/400/403/409 rules,
including the ESP32 quirk that a form-encoded body has no `plain` argument (so it returns 400).
`/` serves the page from `status_page.html`; use `--page path/to/StatusPage.h` to serve the
firmware's current page instead.

Simulated: the opener (press stops if moving, otherwise runs the other way), LAMP flashing
while the motor runs (`--lamp-mode steady`, `--lamp-after N` to change), reed switch closed
within 2% of the floor, boot and Wi-Fi join delay (the device does not answer until it has an
IP), Preferences (control enabled, boot count, saved door position), reset reasons.

Options: `--start closed|open|mid`, `--open-time 14`, `--close-time 13`, `--speed 8` (time
runs faster), `--port`, `--host 127.0.0.1`, `--fresh` (forget saved state), `--quiet`.

## Tests

`python3 test_simulator.py` drives the running simulator over HTTP (52 checks).
The port was also checked against the firmware's own C++ (`GarageDoorModel.cpp`,
`DoorWebCommandPolicy.h`) on 3,000 random sequences (183,000 steps) with no differences.

## Known limits

Not simulated: OTA, mDNS (`garagecontroller.local`), real ESP32 timing and heap, Wi-Fi signal.
Free memory and signal strength are fixed figures. Log timestamps use this computer's clock.
