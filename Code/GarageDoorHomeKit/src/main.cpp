// main.cpp  (GarageDoorHomeKit)
// Garage door open/closed controller for Apple Home, using HomeSpan (native HomeKit).
// Board: Seeed XIAO ESP32C3, Arduino ESP32 core 3.x.
//
// Hardware (schematic rev 10):
//   D1 (GPIO3): door input, 10k pull-up to 3V3, 100nF to GND,
//               1k series + 1N4148 clamps, from J3 (JST PH) -> panel jack -> MC-38 reed.
//   Reed closed (magnet present, door shut) = LOW.
//   D2 (GPIO4): active-HIGH opener button output.
//   D3 (GPIO5): active-LOW FLASH input from PC817.
//   BOOT button (GPIO9): hold 5 s to forget the HomeKit pairing and restart.
//
// Setup: copy include/secrets.example.h to include/secrets.h and fill it in.
// Add to Home: Home app > + > Add Accessory > More options > "Garage Door",
// then enter HOMEKIT_SETUP_CODE.
//
// Web UI: http://garagecontroller.local/ or http://<device IP>/
// Firmware updates over Wi-Fi: pio run -e ota -t upload

#include <Arduino.h>
#include <HomeSpan.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_timer.h>

#include "BootInfo.h"
#include "ClockSync.h"
#include "DebouncedInput.h"
#include "DeviceStatus.h"
#include "DoorControlGate.h"
#include "EventLog.h"
#include "GarageDoorModel.h"
#include "HomeKitDoorController.h"
#include "NetworkStatus.h"
#include "StatusWebServer.h"
#include "secrets.h"

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

const uint8_t DOOR_PIN = 3;     // XIAO D1
const uint8_t BUTTON_PIN = 9;   // XIAO BOOT button
const uint8_t OPENER_TRIGGER_PIN = 4;
const uint8_t OPENER_FLASH_PIN = 5;

#ifndef OPENER_FLASH_FITTED
#define OPENER_FLASH_FITTED 1
#endif

const uint32_t DEBOUNCE_MS = 500;        // door must be stable this long
const uint32_t RESET_HOLD_MS = 5000;     // BOOT held this long = forget pairing
const uint16_t HOMEKIT_PORT = 1201;      // HomeKit traffic; port 80 is the web page
const uint32_t OPENER_PULSE_MS = 500;
const char *const DEVICE_NAME = "garagecontroller";

// Sydney local time, including daylight saving
const char *const TIME_ZONE = "AEST-10AEDT,M10.1.0,M4.1.0/3";

DebouncedInput door(DOOR_PIN, true, DEBOUNCE_MS);   // active (LOW) = closed
HomeKitDoorController *doorService = nullptr;
HomeKitMotionSensor *motionService = nullptr;
GarageDoorModel doorModel(OPENER_FLASH_FITTED);
MotionInput motionInput;
bool triggerActive = false;
uint32_t triggerStart = 0;
esp_timer_handle_t triggerTimer = nullptr;
static bool pressDoor();
static bool saveControlEnabled(bool enabled);
static bool setControlEnabled(bool enabled);
DoorControlGate controlGate(saveControlEnabled);

BootInfo bootInfo;
ClockSync clockSync;
DeviceStatus deviceStatus;
StatusWebServer webServer(Log, clockSync, bootInfo, deviceStatus, pressDoor, setControlEnabled);

static bool saveControlEnabled(bool enabled) {
  Preferences prefs;
  if (!prefs.begin("door-control", false)) return false;
  const bool saved = prefs.putBool("enabled", enabled) == 1;
  prefs.end();
  return saved;
}

static void restoreControlEnabled() {
  Preferences prefs;
  if (!prefs.begin("door-control", false)) {
    Log.log("Door control disabled: could not read saved setting");
    return;
  }
  const uint8_t saved = prefs.getUChar("enabled", 255);
  const bool exists = prefs.isKey("enabled");
  prefs.end();
  if (saved > 1 && exists) {
    Log.log("Door control disabled: invalid saved setting");
    return;
  }
  controlGate.restore(saved == 1);
  deviceStatus.controlEnabled = controlGate.enabled();
  Log.log("Door control %s", controlGate.enabled() ? "enabled" : "disabled");
}

static bool setControlEnabled(bool enabled) {
  if (!enabled) digitalWrite(OPENER_TRIGGER_PIN, LOW);
  const bool saved = controlGate.setEnabled(enabled);
  deviceStatus.controlEnabled = controlGate.enabled();
  if (!saved) {
    digitalWrite(OPENER_TRIGGER_PIN, LOW);
    Log.log("Door control setting save failed; disabled until reboot, previous setting may return");
    return false;
  }
  Log.log("Door control %s from web UI", enabled ? "enabled" : "disabled");
  return true;
}

static bool pressDoor() {
  const uint32_t now = millis();
  if (!controlGate.enabled()) {
    Log.log("Opener press rejected: door control disabled");
    return false;
  }
  if (!triggerTimer) {
    Log.log("Opener press rejected: pulse timer unavailable");
    return false;
  }
  if (triggerActive || !doorModel.canPress()) {
    Log.log("Opener press rejected: busy");
    return false;
  }
  digitalWrite(OPENER_TRIGGER_PIN, HIGH);
  const esp_err_t timerResult = esp_timer_start_once(triggerTimer, OPENER_PULSE_MS * 1000ULL);
  if (timerResult != ESP_OK) {
    digitalWrite(OPENER_TRIGGER_PIN, LOW);
    Log.log("Opener press rejected: pulse timer error %d", (int)timerResult);
    return false;
  }
  doorModel.press(now);
  triggerActive = true;
  triggerStart = now;
  deviceStatus.controlPending = true;
  Log.log("Opener button pressed");
  return true;
}

static void releaseOpener(void *) {
  digitalWrite(OPENER_TRIGGER_PIN, LOW);
}

static void onPairingChanged(boolean paired) {
  Log.log("HomeKit: %s", paired ? "paired with Apple Home" : "pairing removed");
}

static void onConnected(int count) {
  Log.log("Wi-Fi connected to %s, %d dBm (connection #%d)", NetworkStatus::ssid().c_str(),
          NetworkStatus::rssi(), count);
}

static void setupHomeKit() {
  WiFi.mode(WIFI_STA);
  if (!WiFi.setHostname(DEVICE_NAME)) Log.log("Could not set Wi-Fi hostname");

  homeSpan.setLogLevel(0)
      .setPortNum(HOMEKIT_PORT)
      .setWifiCredentials(WIFI_SSID, WIFI_PASSWORD)
      .setPairingCode(HOMEKIT_SETUP_CODE)
      .setHostNameSuffix("")                 // mDNS name: garagecontroller.local
      .setSketchVersion(FIRMWARE_VERSION)
      .setPairCallback(onPairingChanged)
      .setConnectionCallback(onConnected);
  homeSpan.enableOTA(OTA_PASSWORD);          // updates over Wi-Fi, password protected
  homeSpan.begin(Category::Sensors, "Garage Door", DEVICE_NAME, "XIAO-ESP32C3");

  new SpanAccessory();
    new Service::AccessoryInformation();
      new Characteristic::Identify();
      new Characteristic::Manufacturer("Mark Pinnuck");
      new Characteristic::Model("Garage Door Controller");
      new Characteristic::FirmwareRevision(FIRMWARE_VERSION);
    doorService = new HomeKitDoorController(door.active());
    motionService = new HomeKitMotionSensor(false);
    new HomeKitOpenerSwitch(pressDoor);
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
  digitalWrite(OPENER_TRIGGER_PIN, LOW);
  pinMode(OPENER_TRIGGER_PIN, OUTPUT);
  pinMode(OPENER_FLASH_PIN, INPUT);
  Serial.begin(115200);
  delay(500);

  Log.begin();
  deviceStatus.deviceName = DEVICE_NAME;
  Log.log("Device name: %s", deviceStatus.deviceName);
  restoreControlEnabled();
  esp_timer_create_args_t timerArgs = {};
  timerArgs.callback = releaseOpener;
  timerArgs.name = "opener";
  const esp_err_t timerResult = esp_timer_create(&timerArgs, &triggerTimer);
  if (timerResult != ESP_OK) {
    triggerTimer = nullptr;
    Log.log("Could not create opener pulse timer: %d", (int)timerResult);
  }
  bootInfo.begin();
  deviceStatus.homekitSetupCode = HOMEKIT_SETUP_CODE;
  Log.log("Boot #%lu, last reset: %s, firmware %s", (unsigned long)bootInfo.bootCount(),
          bootInfo.resetReason(), FIRMWARE_VERSION);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  door.begin();
  deviceStatus.doorClosed = door.active();
  doorModel.begin(deviceStatus.doorClosed, millis());
  Log.log("Initial door state: %s", deviceStatus.doorClosed ? "CLOSED" : "OPEN");

  setupHomeKit();
}

void loop() {
  if (triggerActive && millis() - triggerStart >= OPENER_PULSE_MS) {
    digitalWrite(OPENER_TRIGGER_PIN, LOW);
    triggerActive = false;
  }
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

  const bool motorRunning = OPENER_FLASH_FITTED &&
      motionInput.update(digitalRead(OPENER_FLASH_PIN) == LOW, millis());
  const auto previousState = doorModel.state();
  const auto event = doorModel.update(deviceStatus.doorClosed, motorRunning, millis());
  if (event == GarageDoorModel::Event::StartFailed) Log.log("Opener did not start");
  if (event == GarageDoorModel::Event::Reversed) Log.log("Opener reversed instead of stopping");
  if (event == GarageDoorModel::Event::SafetyTimeout) Log.log("Opener movement exceeded safety limit");
  if (previousState != doorModel.state())
    Log.log("Door movement state: %s", GarageDoorModel::name(doorModel.state()));
  deviceStatus.doorState = GarageDoorModel::name(doorModel.state());
  deviceStatus.doorMoving = OPENER_FLASH_FITTED ? motorRunning : doorModel.moving();
  deviceStatus.obstruction = doorModel.obstruction();
  deviceStatus.controlPending = !triggerTimer || triggerActive || !doorModel.canPress();
  motionService->setMoving(deviceStatus.doorMoving);

  checkResetButton();
  delay(10);
}
