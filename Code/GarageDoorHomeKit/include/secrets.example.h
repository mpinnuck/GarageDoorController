// secrets.example.h
// Copy this file to secrets.h and fill in your own values.
// secrets.h is in .gitignore so it never goes to GitHub.

#pragma once

// The ESP32-C3 is 2.4 GHz only
#define WIFI_SSID      "MicroConcepts-2G"
#define WIFI_PASSWORD  "your-wifi-password"

// 8-digit code you type into the Home app when adding the sensor.
// Avoid simple ones (12345678, 11111111): HomeKit rejects them.
#define HOMEKIT_SETUP_CODE "46637726"

// Password for firmware updates over Wi-Fi. Put the same value in
// platformio.ini, [env:ota] upload_flags = --auth=...
#define OTA_PASSWORD   "change-me"
