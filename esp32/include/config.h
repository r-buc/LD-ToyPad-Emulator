/*
	Copyright © 2023 Berny23, Cort1237 and many more

	This file is part of "Toy Pad Emulator for Lego Dimensions" which is released under the "MIT" license.
	See file "LICENSE" or go to "https://choosealicense.com/licenses/mit" for full license details.
*/

// Build time configuration of the ESP32-S3 firmware.
// Put your Wi-Fi credentials into include/wifi_secrets.h (see
// include/wifi_secrets.example.h) or pass them as build flags, e.g.
//   PLATFORMIO_BUILD_FLAGS='-DTOYPAD_WIFI_SSID=\"MyWifi\" -DTOYPAD_WIFI_PASSWORD=\"secret\"'

#pragma once

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#endif

// Wi-Fi network to join. If empty or the connection fails, the emulator
// opens its own access point instead.
#ifndef TOYPAD_WIFI_SSID
#define TOYPAD_WIFI_SSID ""
#endif
#ifndef TOYPAD_WIFI_PASSWORD
#define TOYPAD_WIFI_PASSWORD ""
#endif
#ifndef TOYPAD_WIFI_CONNECT_TIMEOUT_MS
#define TOYPAD_WIFI_CONNECT_TIMEOUT_MS 20000
#endif

// Fallback access point. The password must be at least 8 characters long.
#ifndef TOYPAD_AP_SSID
#define TOYPAD_AP_SSID "LD-ToyPad"
#endif
#ifndef TOYPAD_AP_PASSWORD
#define TOYPAD_AP_PASSWORD "legodimensions"
#endif

// mDNS host name: the UI is reachable at http://<hostname>.local/
#ifndef TOYPAD_HOSTNAME
#define TOYPAD_HOSTNAME "ldtoypad"
#endif

#ifndef TOYPAD_HTTP_PORT
#define TOYPAD_HTTP_PORT 80
#endif

// Over-the-air updates (ArduinoOTA) are only enabled when a password is set,
// because the USB port is occupied by the console while the emulator runs.
#ifndef TOYPAD_OTA_PASSWORD
#define TOYPAD_OTA_PASSWORD ""
#endif

// Interval in which queued tag place/remove events are sent to the game
// (same as node-ld).
#ifndef TOYPAD_EVENT_INTERVAL_MS
#define TOYPAD_EVENT_INTERVAL_MS 500
#endif

// Minimum delay between two writes of toytags.json to flash.
#ifndef TOYPAD_SAVE_INTERVAL_MS
#define TOYPAD_SAVE_INTERVAL_MS 1000
#endif
