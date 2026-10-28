// main.cpp  (GarageDoorHomeKit)
// Garage door open/closed sensor for Apple Home, using HomeSpan (native HomeKit).
// Board: Seeed XIAO ESP32C3, Arduino ESP32 core 3.x.
//
// Hardware (schematic rev 10):
//   D1 (GPIO3): door input, 10k pull-up to 3V3, 100nF to GND,
//               1k series + 1N4148 clamps, from J3 (JST PH) -> panel jack -> MC-38 reed.
//   Reed closed (magnet present, door shut) = LOW.
//   BOOT button (GPIO9): hold 5 s to forget the HomeKit pairing and restart.
//
// Setup: copy include/secrets.example.h to include/secrets.h and fill it in.
// Add to Home: Home app > + > Add Accessory > More options > "Garage Door",
// then enter HOMEKIT_SETUP_CODE.
//
// Web UI: http://garagedoor.local/ or http://<device IP>/
// Firmware updates over Wi-Fi: pio run -e ota -t upload

#include <Arduino.h>
#include <HomeSpan.h>
#include <WiFi.h>

#include "BootInfo.h"
#include "ClockSync.h"
#include "DebouncedInput.h"
#include "DeviceStatus.h"
#include "EventLog.h"
#include "HomeKitDoorSensor.h"
#include "NetworkStatus.h"
#include "StatusWebServer.h"
#include "secrets.h"

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

const uint8_t DOOR_PIN = 3;     // XIAO D1
const uint8_t BUTTON_PIN = 9;   // XIAO BOOT button

const uint32_t DEBOUNCE_MS = 500;        // door must be stable this long
const uint32_t RESET_HOLD_MS = 5000;     // BOOT held this long = forget pairing
const uint16_t HOMEKIT_PORT = 1201;      // HomeKit traffic; port 80 is the web page

// Sydney local time, including daylight saving
const char *const TIME_ZONE = "AEST-10AEDT,M10.1.0,M4.1.0/3";

DebouncedInput door(DOOR_PIN, true, DEBOUNCE_MS);   // active (LOW) = closed
HomeKitDoorSensor *doorService = nullptr;

BootInfo bootInfo;
ClockSync clockSync;
DeviceStatus deviceStatus;
StatusWebServer webServer(Log, clockSync, bootInfo, deviceStatus);

static void onPairingChanged(boolean paired) {
  Log.log("HomeKit: %s", paired ? "paired with Apple Home" : "pairing removed");
}

static void onConnected(int count) {
  Log.log("Wi-Fi connected to %s, %d dBm (connection #%d)", NetworkStatus::ssid().c_str(),
          NetworkStatus::rssi(), count);
}

static void setupHomeKit() {
  WiFi.mode(WIFI_STA);
  if (!WiFi.setHostname("garagedoor")) Log.log("Could not set Wi-Fi hostname");

  homeSpan.setLogLevel(0)
      .setPortNum(HOMEKIT_PORT)
      .setWifiCredentials(WIFI_SSID, WIFI_PASSWORD)
      .setPairingCode(HOMEKIT_SETUP_CODE)
      .setHostNameSuffix("")                 // mDNS name: garagedoor.local
      .setSketchVersion(FIRMWARE_VERSION)
      .setPairCallback(onPairingChanged)
      .setConnectionCallback(onConnected);
  homeSpan.enableOTA(OTA_PASSWORD);          // updates over Wi-Fi, password protected
  homeSpan.begin(Category::Sensors, "Garage Door", "GarageDoor", "XIAO-ESP32C3");

  new SpanAccessory();
    new Service::AccessoryInformation();
      new Characteristic::Identify();
      new Characteristic::Manufacturer("Mark Pinnuck");
      new Characteristic::Model("Garage Door Sensor");
      new Characteristic::FirmwareRevision(FIRMWARE_VERSION);
    doorService = new HomeKitDoorSensor(door.active());
}

// Hold BOOT for RESET_HOLD_MS: delete the HomeKit pairing and restart,
// ready to be added to the Home app again.
static void checkResetButton() {
  static uint32_t pressStart = 0;
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (pressStart == 0) pressStart = millis();
    if (millis() - pressStart > RESET_HOLD_MS) {
      Log.log("BOOT held: forgetting HomeKit pairing and restarting");
      delay(200);
      homeSpan.processSerialCommand("H");   // erases pairing data and reboots
    }
  } else {
    pressStart = 0;
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Log.begin();
  bootInfo.begin();
  deviceStatus.homekitSetupCode = HOMEKIT_SETUP_CODE;
  Log.log("Boot #%lu, last reset: %s, firmware %s", (unsigned long)bootInfo.bootCount(),
          bootInfo.resetReason(), FIRMWARE_VERSION);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  door.begin();
  deviceStatus.doorClosed = door.active();
  Log.log("Initial door state: %s", deviceStatus.doorClosed ? "CLOSED" : "OPEN");

  setupHomeKit();
}

void loop() {
  homeSpan.poll();   // HomeKit, Wi-Fi and OTA

  // --- Log Wi-Fi loss ---
  static bool hadIp = false;
  const bool hasIp = NetworkStatus::hasIp();
  if (hadIp && !hasIp) Log.log("Wi-Fi connection lost");
  hadIp = hasIp;

  // --- Network time (for log timestamps), once we have an address ---
  if (!clockSync.started() && hasIp) clockSync.begin(TIME_ZONE);
  static bool timeLogged = false;
  if (!timeLogged && clockSync.isSynced()) {
    timeLogged = true;
    Log.log("Clock synchronised from network time");
  }

  // --- Web UI ---
  webServer.loop();

  // --- Door input (debounced) ---
  if (door.update()) {
    const bool closed = door.active();
    doorService->setClosed(closed);
    deviceStatus.doorClosed = closed;
    deviceStatus.doorChanges++;
    deviceStatus.lastChangeMs = millis();
    deviceStatus.hasChanged = true;
    Log.log("Garage door %s", closed ? "CLOSED" : "OPEN");
  }

  checkResetButton();
  delay(10);
}
