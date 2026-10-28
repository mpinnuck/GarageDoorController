# GarageDoorMatter

Matter (Wi-Fi) contact sensor firmware for the garage door sensor.
Seeed XIAO ESP32C3, door reed on D1 (GPIO3), reed closed = door shut.

## Build (VS Code + PlatformIO)
For a single VS Code window with both the repository and firmware available,
open `GarageDoorSensor.code-workspace` from the repository root. The firmware
folder is included as a separate workspace folder so PlatformIO can find its
`platformio.ini`.

You can also open this folder directly when working only on the firmware.
Uses the pioarduino platform (Arduino ESP32 core 3.x, which has the Matter library).

Run PlatformIO CLI commands from this folder:

```sh
pio run -t erase
pio run -t upload
pio device monitor
```

If the board is not detected, hold BOOT while plugging in USB.

## Web UI
Once the sensor is on Wi-Fi, open `http://<device IP>/` in any browser on your
home network. It shows the door state, Wi-Fi signal, uptime, boot count, last
reset reason and a live event log (updated every 2 s, last 100 lines kept on
the device, Download button saves what the page has collected).

Find the IP in the Deco app's client list, and reserve it there so it
never changes. The page is read-only and has no login, so it is only
reachable from your home network.

Source files:
- `EventLog`       ring buffer of log lines, also mirrored to Serial
- `StatusWebServer` port 80 server: `/`, `/api/status`, `/api/log?since=N`, `POST /api/reset-boot-count`
- `StatusPage.h`   the HTML/JS page
- `ClockSync`      NTP time (Sydney time zone) for log timestamps
- `NetworkStatus`  IP, SSID and signal from the Wi-Fi interface Matter owns
- `BootInfo`       persistent boot counter and last reset reason
- `DeviceStatus`   door state shared with the web server

## Pairing
Serial monitor prints a manual pairing code and a QR code link.
- Apple Home: Add Accessory, scan the QR code or enter the code.
- Google Home: in Apple Home, accessory settings > Turn On Pairing Mode,
  then add the new code in Google Home > Add device > Matter.

Hold BOOT for 5 s to decommission (factory reset Matter).
