// secrets.example.h
// Copy this file to secrets.h and fill in your own values.
// secrets.h is in .gitignore so it never goes to GitHub.

#pragma once

// The ESP32-C3 is 2.4 GHz only
#define WIFI_SSID      "MicroConcepts-2G"
#define WIFI_PASSWORD  "your-wifi-password"

// Password for firmware updates over Wi-Fi. Set the OTA_PASSWORD environment
// variable to the same value when uploading.
#define OTA_PASSWORD   "change-me"
