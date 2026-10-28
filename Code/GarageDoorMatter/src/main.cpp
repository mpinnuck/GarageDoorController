// main.cpp  (GarageDoorMatter)
// Garage door open/closed sensor as a Matter contact sensor (Wi-Fi).
// Board: Seeed XIAO ESP32C3, Arduino ESP32 core 3.x (Matter library).
//
// Hardware (schematic rev 10):
//   D1 (GPIO3): door input, 10k pull-up to 3V3, 100nF to GND,
//               1k series + 1N4148 clamps, from J3 (JST PH) -> panel jack -> MC-38 reed.
//   Reed closed (magnet present, door shut) = LOW.
//   BOOT button (GPIO9): hold 5 s to decommission (factory reset Matter).
//
// Commissioning: over Bluetooth (CHIPoBLE). The Apple/Google app sends the
// Wi-Fi credentials, so none are stored in this sketch.
//
// Web UI: once on Wi-Fi, browse to http://<device IP>/ for live status and
// the event log. The IP is printed in the log and shown in the Deco app.
//
// Build settings (partition scheme etc.) are in platformio.ini.

#include <Arduino.h>
#include <Matter.h>

#include "BootInfo.h"
#include "ClockSync.h"
#include "DeviceStatus.h"
#include "EventLog.h"
#include "NetworkStatus.h"
#include "StatusWebServer.h"

MatterContactSensor GarageDoor;

const uint8_t DOOR_PIN = 3;     // XIAO D1
const uint8_t BUTTON_PIN = 9;   // XIAO BOOT button

const uint32_t DEBOUNCE_MS = 500;        // door must be stable this long
const uint32_t DECOMMISSION_HOLD_MS = 5000;

// Sydney local time, including daylight saving
const char *const TIME_ZONE = "AEST-10AEDT,M10.1.0,M4.1.0/3";

BootInfo bootInfo;
ClockSync clockSync;
DeviceStatus deviceStatus;
StatusWebServer webServer(Log, clockSync, bootInfo, deviceStatus);

// Reed closed (LOW) = door shut = contact closed (true)
static bool readDoorClosed() {
  return digitalRead(DOOR_PIN) == LOW;
}

static void printPairingInfo() {
  Log.log("Matter device not commissioned yet.");
  Log.log("Manual pairing code: %s", Matter.getManualPairingCode().c_str());
  Log.log("QR code: %s", Matter.getOnboardingQRCodeUrl().c_str());
}

// Matter stack events worth recording. Runs on the Matter task; EventLog is thread-safe.
static void onMatterEvent(matterEvent_t event, const chip::DeviceLayer::ChipDeviceEvent *) {
  switch (event) {
    case MATTER_COMMISSIONING_COMPLETE:    Log.log("Matter: commissioning complete"); break;
    case MATTER_COMMISSIONING_WINDOW_OPEN: Log.log("Matter: pairing window opened"); break;
    case MATTER_COMMISSIONING_WINDOW_CLOSED: Log.log("Matter: pairing window closed"); break;
    case MATTER_FABRIC_COMMITTED:          Log.log("Matter: added to a controller (fabric committed)"); break;
    case MATTER_FABRIC_REMOVED:            Log.log("Matter: removed from a controller (fabric removed)"); break;
    case MATTER_FAIL_SAFE_TIMER_EXPIRED:   Log.log("Matter: pairing attempt timed out"); break;
    case MATTER_INTERFACE_IP_ADDRESS_CHANGED: Log.log("Matter: IP address changed"); break;
    case MATTER_SERVER_READY:              Log.log("Matter: server ready"); break;
    case MATTER_FACTORY_RESET:             Log.log("Matter: factory reset"); break;
    default: break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Log.begin();
  bootInfo.begin();
  Log.log("Boot #%lu, last reset: %s", (unsigned long)bootInfo.bootCount(), bootInfo.resetReason());

  pinMode(DOOR_PIN, INPUT);            // external 10k pull-up fitted
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Matter.setProductName("Garage Door");   // must be set before Matter.begin()
  Matter.setDeviceName("Garage Door");
  Matter.onEvent(onMatterEvent);          // must be registered before Matter.begin()

  GarageDoor.begin();                  // endpoint first...
  Matter.begin();                      // ...then the Matter stack

  // Boolean-state sensors must be set after Matter.begin()
  GarageDoor.setContact(readDoorClosed());
  deviceStatus.doorClosed = GarageDoor.getContact();
  Log.log("Initial door state: %s", GarageDoor ? "CLOSED" : "OPEN");

  if (!Matter.isDeviceCommissioned()) {
    printPairingInfo();
    uint32_t lastPrint = millis();
    while (!Matter.isDeviceCommissioned()) {
      delay(100);
      if (millis() - lastPrint > 30000) {   // reprint every 30 s
        printPairingInfo();
        lastPrint = millis();
      }
    }
    Log.log("Commissioned. Connected to the Matter fabric.");
  }
}

void loop() {
  // --- Log Wi-Fi connect/disconnect ---
  static int lastWifi = -1;
  int wifiNow = Matter.isWiFiConnected() ? 1 : 0;
  if (wifiNow != lastWifi) {
    lastWifi = wifiNow;
    if (wifiNow) {
      Log.log("Wi-Fi CONNECTED to %s, %d dBm", NetworkStatus::ssid().c_str(), NetworkStatus::rssi());
    } else {
      Log.log("Wi-Fi not connected");
    }
  }

  // --- Network time (for log timestamps), once we have an address ---
  if (!clockSync.started() && NetworkStatus::hasIp()) {
    clockSync.begin(TIME_ZONE);
  }
  static bool timeLogged = false;
  if (!timeLogged && clockSync.isSynced()) {
    timeLogged = true;
    Log.log("Clock synchronised from network time");
  }

  // --- Web UI ---
  webServer.loop();

  // --- Door input with debounce ---
  static bool lastRaw = readDoorClosed();
  static uint32_t lastChange = millis();

  bool raw = readDoorClosed();
  if (raw != lastRaw) {
    lastRaw = raw;
    lastChange = millis();
  }
  if ((millis() - lastChange) >= DEBOUNCE_MS && raw != GarageDoor.getContact()) {
    GarageDoor.setContact(raw);
    deviceStatus.doorClosed = raw;
    deviceStatus.doorChanges++;
    deviceStatus.lastChangeMs = millis();
    deviceStatus.hasChanged = true;
    Log.log("Garage door %s", raw ? "CLOSED" : "OPEN");
  }

  // --- BOOT button long hold: decommission ---
  static uint32_t pressStart = 0;
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (pressStart == 0) pressStart = millis();
    if (millis() - pressStart > DECOMMISSION_HOLD_MS) {
      Log.log("Decommissioning. The device must be added again.");
      Matter.decommission();          // factory resets and restarts
      pressStart = 0;
    }
  } else {
    pressStart = 0;
  }

  delay(20);
}
