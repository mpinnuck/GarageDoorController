// Garage door open/closed controller with a web UI and HTTP API.
// Board: Seeed XIAO ESP32C3, Arduino ESP32 core 3.x.
//
// Hardware:
//   D1 (GPIO3): door input, 10k pull-up to 3V3, 100nF to GND,
//               1k series + 1N4148 clamps, from J3 (JST PH) -> panel jack -> MC-38 reed.
//   Reed closed (magnet present, door shut) = LOW.
//   D2 (GPIO4): active-HIGH opener button output.
//   D3 (GPIO5): active-LOW LAMP/motor indicator input from PC817.
//
// Setup: copy include/secrets.example.h to include/secrets.h and fill it in.
//
// Web UI: http://garagecontroller.local/ or http://<device IP>/
// Firmware updates over Wi-Fi: pio run -e ota -t upload

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_timer.h>

#include "BootInfo.h"
#include "ClockSync.h"
#include "DebouncedInput.h"
#include "DeviceStatus.h"
#include "DoorControlGate.h"
#include "DoorPositionRestore.h"
#include "DoorPositionStore.h"
#include "EventLog.h"
#include "GarageDoorModel.h"
#include "NetworkStatus.h"
#include "StatusWebServer.h"
#include "secrets.h"

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif
static_assert(sizeof(GARAGE_CONTROL_TOKEN) >= 33,
              "Set GARAGE_CONTROL_TOKEN in secrets.h to a random token of at least 32 characters");

const uint8_t DOOR_PIN = 3;     // XIAO D1
const uint8_t OPENER_TRIGGER_PIN = 4;
const uint8_t OPENER_FLASH_PIN = 5;

#ifndef OPENER_FLASH_FITTED
#define OPENER_FLASH_FITTED 1
#endif

const uint32_t DEBOUNCE_MS = 500;        // door must be stable this long
const uint32_t OPENER_PULSE_MS = 500;
const char *const DEVICE_NAME = "garagecontroller";

// Sydney local time, including daylight saving
const char *const TIME_ZONE = "AEST-10AEDT,M10.1.0,M4.1.0/3";

DebouncedInput door(DOOR_PIN, true, DEBOUNCE_MS);   // active (LOW) = closed
GarageDoorModel doorModel(OPENER_FLASH_FITTED);
MotionInput motionInput;
bool triggerActive = false;
uint32_t triggerStart = 0;
uint32_t inputMonitoringStartedAt = 0;
esp_timer_handle_t triggerTimer = nullptr;
DoorPositionStore positionStore;
bool positionStorageHealthy = true;
bool positionRestorePending = false;
bool positionStartupSettled = false;
bool flashObserved = false;
uint32_t lastFlashAt = 0;

/**
 * @brief Attempts a raw opener-button press through the control gate.
 * @return True if the press was accepted; otherwise, false.
 */
static bool pressDoor();

/**
 * @brief Evaluates and, when allowed, executes a conditional door command.
 * @param[in] command Door command requested by the caller.
 * @return The command outcome and any applicable explanation.
 */
static DoorWebCommandPolicy::Result commandDoor(DoorWebCommandPolicy::Command command);

/**
 * @brief Persists the requested control-enabled state.
 * @param[in] enabled State to save.
 * @return True if the state was saved; otherwise, false.
 */
static bool saveControlEnabled(bool enabled);

/**
 * @brief Applies and persists the requested door-control state.
 * @param[in] enabled True to enable control, or false to disable it.
 * @return True if the state was saved and applied; otherwise, false.
 */
static bool setControlEnabled(bool enabled);
DoorControlGate controlGate(saveControlEnabled);

BootInfo bootInfo;
ClockSync clockSync;
DeviceStatus deviceStatus;
StatusWebServer webServer(Log, clockSync, bootInfo, deviceStatus, GARAGE_CONTROL_TOKEN, pressDoor, commandDoor,
                          setControlEnabled);

/**
 * @brief Clears the persisted position and reports storage failures.
 * @return True if the position was cleared or no record needed clearing;
 *         otherwise, false.
 */
static bool clearSavedPosition() {
  positionRestorePending = false;
  if (!positionStorageHealthy) return false;
  positionStorageHealthy = positionStore.clear();
  if (!positionStorageHealthy) {
    Log.log("Door position storage error: could not clear record; opener control disabled");
    return false;
  }
  return true;
}

/**
 * @brief Saves the current door position when startup and sensor state are settled.
 */
static void saveSettledPosition() {
  if (!positionStorageHealthy || !positionStartupSettled || !OPENER_FLASH_FITTED) return;
  GarageDoorModel::Position position;
  if (!doorModel.position(position)) {
    clearSavedPosition();
    return;
  }
  positionStorageHealthy = positionStore.save(position);
  if (!positionStorageHealthy) {
    Log.log("Door position storage error: could not save record; opener control disabled");
    return;
  }
}

/**
 * @brief Maps the ESP reset cause to a position-restore reset category.
 * @param[in] reason ESP reset cause to translate.
 * @return Corresponding position-restore reset category.
 */
static DoorPositionRestore::Reset positionResetReason(esp_reset_reason_t reason) {
  using Reset = DoorPositionRestore::Reset;
  switch (reason) {
    case ESP_RST_SW: return Reset::Software;
    case ESP_RST_PANIC: return Reset::Panic;
    case ESP_RST_INT_WDT: return Reset::InterruptWatchdog;
    case ESP_RST_TASK_WDT: return Reset::TaskWatchdog;
    case ESP_RST_WDT: return Reset::Watchdog;
    case ESP_RST_USB: return Reset::Usb;
    case ESP_RST_POWERON: return Reset::PowerOn;
    case ESP_RST_BROWNOUT: return Reset::Brownout;
    default: return Reset::Other;
  }
}

/**
 * @brief Loads and conditionally schedules restoration of a saved door position.
 */
static void loadSavedPosition() {
  if (!positionStore.load()) {
    positionStorageHealthy = false;
    Log.log("Door position not restored: storage unavailable; opener control disabled");
    return;
  }
  if (!OPENER_FLASH_FITTED) {
    Log.log("Door position not restored: motion input not fitted");
    clearSavedPosition();
    return;
  }
  using Decision = DoorPositionRestore::Decision;
  const Decision decision = DoorPositionRestore::decide(
      positionResetReason(esp_reset_reason()), deviceStatus.doorClosed,
      positionStore.present(), positionStore.record());
  if (decision == Decision::Restore) {
    positionRestorePending = true;
    return;
  }
  const char *reason = decision == Decision::ReedClosed ? "reed closed" :
                       decision == Decision::ResetNotEligible ? "reset reason" :
                       decision == Decision::NoRecord ? "no saved record" : "invalid record";
  Log.log("Door position not restored: %s", reason);
  clearSavedPosition();
}

static bool saveControlEnabled(bool enabled) {
  Preferences prefs;
  if (!prefs.begin("door-control", false)) return false;
  const bool saved = prefs.putBool("enabled", enabled) == 1;
  prefs.end();
  return saved;
}

/**
 * @brief Restores the persisted control-enabled state, defaulting to disabled.
 */
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

/**
 * @brief Validates and issues a raw opener-button pulse.
 * @return True if the pulse was started; otherwise, false.
 */
static bool pressDoorInternal() {
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
  if (!clearSavedPosition()) {
    Log.log("Opener press rejected: door position storage unavailable");
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

static bool pressDoor() { return pressDoorInternal(); }

static DoorWebCommandPolicy::Result commandDoor(DoorWebCommandPolicy::Command command) {
  const auto result = DoorWebCommandPolicy::evaluate(doorModel, command);
  if (result.action != DoorWebCommandPolicy::Action::Press) return result;
  return pressDoorInternal() ? result
                             : DoorWebCommandPolicy::Result{DoorWebCommandPolicy::Action::Failed,
                                                            "opener press failed"};
}

/**
 * @brief Ends the opener pulse from the timer callback.
 * @param[in] context Unused timer callback context.
 */
static void releaseOpener(void *) {
  digitalWrite(OPENER_TRIGGER_PIN, LOW);
}

/**
 * @brief Configures Wi-Fi station mode and password-protected OTA updates.
 */
static void setupNetwork() {
  WiFi.mode(WIFI_STA);
  if (!WiFi.setHostname(DEVICE_NAME)) Log.log("Could not set Wi-Fi hostname");
  if (!WiFi.setAutoReconnect(true)) Log.log("Could not enable Wi-Fi auto-reconnect");
  if (!WiFi.setSleep(false)) Log.log("Could not disable Wi-Fi sleep");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  ArduinoOTA.setHostname(DEVICE_NAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
}

/**
 * @brief Initializes hardware, persistent state, services, and network setup.
 */
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
  Log.log("Boot #%lu, last reset: %s, firmware %s", (unsigned long)bootInfo.bootCount(),
          bootInfo.resetReason(), FIRMWARE_VERSION);

  door.begin();
  deviceStatus.doorClosed = door.active();
  doorModel.begin(deviceStatus.doorClosed, millis());
  Log.log("Initial door state: %s", deviceStatus.doorClosed ? "CLOSED" : "OPEN");
  loadSavedPosition();

  setupNetwork();
  inputMonitoringStartedAt = millis();
}

/**
 * @brief Runs the controller's periodic sensor, network, and web-server work.
 */
void loop() {
  if (triggerActive && millis() - triggerStart >= OPENER_PULSE_MS) {
    digitalWrite(OPENER_TRIGGER_PIN, LOW);
    triggerActive = false;
  }
  // --- Log Wi-Fi loss ---
  static bool hadIp = false;
  const bool hasIp = NetworkStatus::hasIp();
  static bool otaStarted = false;
  static bool mdnsStarted = false;
  if (hasIp && !hadIp) {
    Log.log("Wi-Fi connected to %s, %d dBm", NetworkStatus::ssid().c_str(),
            NetworkStatus::rssi());
    mdnsStarted = MDNS.begin(DEVICE_NAME);
    if (!mdnsStarted) Log.log("Could not start mDNS");
    if (!otaStarted) {
      ArduinoOTA.begin();
      otaStarted = true;
      Log.log("Password-protected OTA updates ready");
    }
  }
  if (hadIp && !hasIp) {
    Log.log("Wi-Fi connection lost");
    if (mdnsStarted) MDNS.end();
    mdnsStarted = false;
  }
  hadIp = hasIp;
  if (otaStarted) ArduinoOTA.handle();

  // --- Network time (for log timestamps), once we have an address ---
  if (!clockSync.started() && hasIp) clockSync.begin(TIME_ZONE);
  static bool timeLogged = false;
  if (!timeLogged && clockSync.isSynced()) {
    timeLogged = true;
    Log.log("Clock synchronised from network time");
  }

  // --- Door input (debounced) ---
  if (door.update()) {
    const bool closed = door.active();
    deviceStatus.doorClosed = closed;
    deviceStatus.doorChanges++;
    deviceStatus.lastChangeMs = millis();
    deviceStatus.hasChanged = true;
    Log.log("Garage door %s", closed ? "CLOSED" : "OPEN");
  }

  const bool flashLit = OPENER_FLASH_FITTED && digitalRead(OPENER_FLASH_PIN) == LOW;
  if (flashLit) {
    flashObserved = true;
    lastFlashAt = millis();
  }
  if (flashLit || deviceStatus.doorClosed || digitalRead(DOOR_PIN) == LOW) {
    if (positionRestorePending && flashLit) Log.log("Door position not restored: motion detected");
    clearSavedPosition();
  }
  const bool motorRunning = OPENER_FLASH_FITTED && motionInput.update(flashLit, millis());
  const auto previousState = doorModel.state();
  const auto event = doorModel.update(deviceStatus.doorClosed, motorRunning, millis());
  if (event == GarageDoorModel::Event::StartFailed) Log.log("Opener did not start");
  if (event == GarageDoorModel::Event::Reversed) Log.log("Opener reversed instead of stopping");
  if (event == GarageDoorModel::Event::SafetyTimeout) Log.log("Opener movement exceeded safety limit");
  if (previousState != doorModel.state())
    Log.log("Door movement state: %s", GarageDoorModel::name(doorModel.state()));
  if (!positionStartupSettled && millis() - inputMonitoringStartedAt >= 1500) {
    positionStartupSettled = true;
    if (positionRestorePending) {
      positionRestorePending = false;
      if (doorModel.restorePosition(positionStore.record()))
        Log.log("Door position restored after restart");
      else {
        Log.log("Door position not restored: door state changed");
        clearSavedPosition();
      }
    }
  }
  if (positionStartupSettled && digitalRead(DOOR_PIN) != LOW &&
      (!flashObserved || millis() - lastFlashAt >= 1500 || motorRunning))
    saveSettledPosition();
  deviceStatus.doorState = GarageDoorModel::name(doorModel.state());
  deviceStatus.doorMoving = OPENER_FLASH_FITTED ? motorRunning : doorModel.moving();
  deviceStatus.flashMoving = motorRunning;
  deviceStatus.obstruction = doorModel.obstruction();
  deviceStatus.controlPending = !positionStorageHealthy || !triggerTimer ||
                                triggerActive || !doorModel.canPress();
  webServer.loop();
  delay(10);
}
