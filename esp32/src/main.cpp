/*
	Copyright © 2023 Berny23, Cort1237 and many more

	This file is part of "Toy Pad Emulator for Lego Dimensions" which is released under the "MIT" license.
	See file "LICENSE" or go to "https://choosealicense.com/licenses/mit" for full license details.
*/

// Toy Pad Emulator for Lego Dimensions - ESP32-S3 firmware entry point.
//
//   Browser  --Wi-Fi (REST + Server-Sent Events)-->  ESP32-S3  --USB HID (TinyUSB)-->  Console / PC

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WiFi.h>

#include "ToyPadService.h"
#include "UsbToyPad.h"
#include "WebApi.h"
#include "config.h"

namespace {

UsbToyPad usbToyPad;
AsyncWebServer server(TOYPAD_HTTP_PORT);
AsyncEventSource events("/events");
ToyPadService service;

bool apMode = false;
bool networkUp = false;
uint32_t wifiStartedAt = 0;
uint32_t lastEventFlush = 0;

void startAccessPoint() {
  log_i("Starting access point '%s'", TOYPAD_AP_SSID);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(TOYPAD_AP_SSID, TOYPAD_AP_PASSWORD);
  apMode = true;
}

void startWifi() {
  WiFi.setHostname(TOYPAD_HOSTNAME);
  if (strlen(TOYPAD_WIFI_SSID) == 0) {
    startAccessPoint();
    return;
  }
  log_i("Connecting to '%s'", TOYPAD_WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // lower latency for the web UI
  WiFi.begin(TOYPAD_WIFI_SSID, TOYPAD_WIFI_PASSWORD);
  wifiStartedAt = millis();
}

void onNetworkUp() {
  networkUp = true;
  IPAddress ip = apMode ? WiFi.softAPIP() : WiFi.localIP();
  Serial.printf("Toy Pad Emulator ready: http://%s/ or http://%s.local/\n", ip.toString().c_str(), TOYPAD_HOSTNAME);

  if (strlen(TOYPAD_OTA_PASSWORD) > 0) {
    ArduinoOTA.setHostname(TOYPAD_HOSTNAME);
    ArduinoOTA.setPassword(TOYPAD_OTA_PASSWORD);
    ArduinoOTA.begin(); // also starts mDNS
  } else {
    MDNS.begin(TOYPAD_HOSTNAME);
  }
  MDNS.addService("http", "tcp", TOYPAD_HTTP_PORT);
}

// Non-blocking Wi-Fi supervision so USB requests are never delayed by it.
void handleWifi() {
  if (networkUp) return;
  if (apMode) {
    onNetworkUp();
  } else if (WiFi.status() == WL_CONNECTED) {
    onNetworkUp();
  } else if (millis() - wifiStartedAt > TOYPAD_WIFI_CONNECT_TIMEOUT_MS) {
    log_w("Could not connect to '%s'", TOYPAD_WIFI_SSID);
    startAccessPoint();
  }
}

} // namespace

void setup() {
  Serial.begin(115200);

  // Start USB first so the console sees the Toy Pad as early as possible.
  // Requests are queued until loop() runs.
  usbToyPad.begin();

  if (!LittleFS.begin(true)) log_e("Failed to mount LittleFS");

  service.begin([](const uint8_t *report) { return usbToyPad.send(report); },
                [](const char *event, const String &json) { events.send(json.c_str(), event, 0); });

  startWifi();
  setupWebApi(server, events, service);
  server.begin();
}

void loop() {
  uint8_t report[ld::REPORT_SIZE];
  // Answer the game first; wait briefly for new requests to keep latency low,
  // but bound the batch so housekeeping below is never starved.
  for (int i = 0; i < 8 && usbToyPad.receive(report, pdMS_TO_TICKS(2)); i++) service.handleReport(report, sizeof(report));

  if (millis() - lastEventFlush >= TOYPAD_EVENT_INTERVAL_MS) {
    lastEventFlush = millis();
    service.flushEvents();
  }

  service.persist();
  handleWifi();
  if (networkUp && strlen(TOYPAD_OTA_PASSWORD) > 0) ArduinoOTA.handle();
}
