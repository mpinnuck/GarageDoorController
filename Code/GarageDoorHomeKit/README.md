# GarageDoorHomeKit

Apple HomeKit contact sensor firmware for the garage door sensor, using
[HomeSpan](https://github.com/HomeSpan/HomeSpan). Replaces `../GarageDoorMatter`
(kept unchanged). Seeed XIAO ESP32C3, door reed on D1 (GPIO3), reed closed = door shut.

- Apple Home shows **Garage Door** as Open / Closed; Siri: "Is the garage door open?"
- Same web page as before at `http://garagedoor.local/` (or the reserved IP);
  the device advertises the Wi-Fi hostname `garagedoor` (the router may show it
  with its own local DNS suffix, such as `.lan`)
- **Firmware updates over Wi-Fi** once installed

HomeKit only: Google Home can no longer see the sensor.

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

## Reset
Hold BOOT for 5 s: forgets the HomeKit pairing and restarts, ready to add again.

## Web UI
Door state, Wi-Fi signal, uptime, boot count (with Reset), last reset reason,
HomeKit pairing and setup code, and a live event log. Read-only apart from the
boot-count reset.

Source files:
- `main.cpp`          HomeSpan setup, door input, BOOT reset
- `HomeKitDoorSensor` HomeKit Contact Sensor service
- `DebouncedInput`    reed switch with 500 ms debounce
- `EventLog`          ring buffer of log lines, also mirrored to Serial
- `StatusWebServer`   port 80: `/`, `/api/status`, `/api/log?since=N`, `POST /api/reset-boot-count`
- `StatusPage.h`      the HTML/JS page
- `ClockSync`         NTP time (Sydney time zone) for log timestamps
- `NetworkStatus`     IP, SSID and signal from the Wi-Fi interface
- `BootInfo`          persistent boot counter and last reset reason
- `DeviceStatus`      door state shared with the web server
