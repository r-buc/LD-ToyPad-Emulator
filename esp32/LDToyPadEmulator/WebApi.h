/*
	Copyright © 2023 Berny23, Cort1237 and many more

	This file is part of "Toy Pad Emulator for Lego Dimensions" which is released under the "MIT" license.
	See file "LICENSE" or go to "https://choosealicense.com/licenses/mit" for full license details.
*/

#pragma once

#include <ESPAsyncWebServer.h>

#include "ToyPadService.h"

// Registers the REST endpoints (same routes as the Node.js server), the
// Server-Sent Events stream that replaces Socket.IO and the static web UI.
void setupWebApi(AsyncWebServer &server, AsyncEventSource &events, ToyPadService &toyPadService);
