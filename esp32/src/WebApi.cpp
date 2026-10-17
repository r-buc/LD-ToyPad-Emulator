/*
	Copyright © 2023 Berny23, Cort1237 and many more

	This file is part of "Toy Pad Emulator for Lego Dimensions" which is released under the "MIT" license.
	See file "LICENSE" or go to "https://choosealicense.com/licenses/mit" for full license details.
*/

#include "WebApi.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

namespace {

constexpr size_t MAX_BODY_SIZE = 512;

// Collects the (small) JSON request body into request->_tempObject, which
// AsyncWebServerRequest frees when the request is destroyed.
void collectBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  if (total > MAX_BODY_SIZE) return;
  if (index == 0 && !request->_tempObject) request->_tempObject = calloc(total + 1, 1);
  if (request->_tempObject && index + len <= total) memcpy(static_cast<uint8_t *>(request->_tempObject) + index, data, len);
}

bool parseBody(AsyncWebServerRequest *request, JsonDocument &doc) {
  if (!request->_tempObject) return false;
  return !deserializeJson(doc, static_cast<const char *>(request->_tempObject));
}

void sendResult(AsyncWebServerRequest *request, ToyPadService::Result result, const String &body = "") {
  switch (result) {
  case ToyPadService::Result::Ok:
    request->send(200, "text/plain", body);
    break;
  case ToyPadService::Result::BadRequest:
    request->send(400, "text/plain", "Bad request");
    break;
  case ToyPadService::Result::NotFound:
    request->send(404, "text/plain", "Unknown token");
    break;
  case ToyPadService::Result::Conflict:
    request->send(409, "text/plain", "Position occupied or token already placed");
    break;
  case ToyPadService::Result::StorageFull:
    request->send(507, "text/plain", "Too many tokens, delete some first");
    break;
  }
}

// Reads an unsigned integer field in [min, max]; returns false if missing or out of range.
bool readUInt(JsonDocument &doc, const char *key, uint32_t min, uint32_t max, uint32_t &out) {
  JsonVariant v = doc[key];
  if (!v.is<long>()) return false;
  long value = v.as<long>();
  if (value < static_cast<long>(min) || value > static_cast<long>(max)) return false;
  out = static_cast<uint32_t>(value);
  return true;
}

using JsonHandler = std::function<void(AsyncWebServerRequest *, JsonDocument &)>;

void onJson(AsyncWebServer &server, const char *uri, WebRequestMethodComposite method, JsonHandler handler) {
  server.on(
    uri, method,
    [handler](AsyncWebServerRequest *request) {
      JsonDocument doc;
      if (!parseBody(request, doc)) {
        request->send(400, "text/plain", "Expected a JSON body");
        return;
      }
      handler(request, doc);
    },
    nullptr, collectBody
  );
}

} // namespace

void setupWebApi(AsyncWebServer &server, AsyncEventSource &events, ToyPadService &service) {
  // Real-time updates (replaces Socket.IO). Late joiners get the connection state immediately.
  events.onConnect([&service](AsyncEventSourceClient *client) {
    if (service.connected()) client->send("null", "Connection True", 0);
  });
  server.addHandler(&events);

  // Served from RAM so the UI always sees the current state (registered before the static handler).
  server.on("/json/toytags.json", HTTP_GET, [&service](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", service.toyTagsJson());
    response->addHeader("Cache-Control", "no-cache");
    request->send(response);
  });

  // Create a new character and save it to toytags.json
  onJson(server, "/character", HTTP_POST, [&service](AsyncWebServerRequest *request, JsonDocument &doc) {
    uint32_t id;
    if (!readUInt(doc, "id", 1, 0xFFFF, id)) return request->send(400, "text/plain", "Invalid id");
    std::string uid;
    ToyPadService::Result result = service.createCharacter(static_cast<uint16_t>(id), uid);
    sendResult(request, result, uid.c_str());
  });

  // Create a new vehicle/gadget and save it to toytags.json, responds with its UID
  onJson(server, "/vehicle", HTTP_POST, [&service](AsyncWebServerRequest *request, JsonDocument &doc) {
    uint32_t id;
    if (!readUInt(doc, "id", 1, 0xFFFF, id)) return request->send(400, "text/plain", "Invalid id");
    std::string uid;
    ToyPadService::Result result = service.createVehicle(static_cast<uint16_t>(id), uid);
    sendResult(request, result, uid.c_str());
  });

  // Called when a token is placed or moved onto a position of the Toy Pad
  onJson(server, "/place", HTTP_POST, [&service](AsyncWebServerRequest *request, JsonDocument &doc) {
    uint32_t position, index;
    const char *uid = doc["uid"];
    if (!uid || !readUInt(doc, "position", 1, 3, position) || !readUInt(doc, "index", 1, ld::MAX_PAD_INDEX, index))
      return request->send(400, "text/plain", "Invalid uid, position or index");
    sendResult(request, service.place(uid, static_cast<uint8_t>(position), static_cast<uint8_t>(index)));
  });

  // Called when a token needs to be removed from the pad
  onJson(server, "/remove", HTTP_DELETE, [&service](AsyncWebServerRequest *request, JsonDocument &doc) {
    uint32_t index;
    if (!readUInt(doc, "index", 1, ld::MAX_PAD_INDEX, index)) return request->send(400, "text/plain", "Invalid index");
    const char *uid = doc["uid"] | "";
    sendResult(request, service.remove(static_cast<uint8_t>(index), uid), "true");
  });

  // Replaces the Socket.IO 'deleteToken' message
  onJson(server, "/token", HTTP_DELETE, [&service](AsyncWebServerRequest *request, JsonDocument &doc) {
    const char *uid = doc["uid"];
    if (!uid) return request->send(400, "text/plain", "Invalid uid");
    sendResult(request, service.deleteToken(uid));
  });

  // Replaces the Socket.IO 'syncToyPad' message
  server.on("/sync", HTTP_POST, [&service](AsyncWebServerRequest *request) {
    service.sync();
    request->send(200, "text/plain", "");
  });

  // Replaces the Socket.IO 'connectionStatus' message
  server.on("/status", HTTP_GET | HTTP_POST, [&service, &events](AsyncWebServerRequest *request) {
    if (request->method() == HTTP_POST && service.connected()) events.send("null", "Connection True", 0);
    request->send(200, "application/json", service.statusJson());
  });

  // The shared index.html also references the Socket.IO client of the Node.js
  // server. Answer with a no-op script so the UI falls back to Server-Sent Events.
  server.on("/socket.io/socket.io.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/javascript", "/* Socket.IO is not used on ESP32-S3, see /events */\n");
  });

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  server.onNotFound([](AsyncWebServerRequest *request) { request->send(404, "text/plain", "Not found"); });
}
