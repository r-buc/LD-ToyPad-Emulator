/*
	Copyright © 2023 Berny23, Cort1237 and many more

	This file is part of "Toy Pad Emulator for Lego Dimensions" which is released under the "MIT" license.
	See file "LICENSE" or go to "https://choosealicense.com/licenses/mit" for full license details.
*/

#include "WebApi.h"

#include <LittleFS.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace {

constexpr size_t MAX_BODY_SIZE = 512;

ToyPadService *service = nullptr;

// Collects the (small) JSON request body into request->_tempObject, which
// AsyncWebServerRequest frees when the request is destroyed.
void collectBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  if (total > MAX_BODY_SIZE) return;
  if (index == 0 && !request->_tempObject) request->_tempObject = calloc(total + 1, 1);
  if (request->_tempObject && index + len <= total) memcpy(static_cast<uint8_t *>(request->_tempObject) + index, data, len);
}

void sendResult(AsyncWebServerRequest *request, ToyPadService::Result result, const char *body = "") {
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

// The web UI sends flat JSON objects with numbers and short strings, e.g.
// {"uid":"04A1B2C3D4E580","position":1,"index":2}. Instead of a JSON library
// the values are looked up directly in the body.

const char *skipSpace(const char *p) {
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  return p;
}

// Returns a pointer to the value of "key" or nullptr if the key is missing.
const char *findValue(const char *body, const char *key) {
  size_t keyLen = strlen(key);
  for (const char *p = strchr(body, '"'); p; p = strchr(p + 1, '"')) {
    if (strncmp(p + 1, key, keyLen) != 0 || p[keyLen + 1] != '"') continue;
    const char *v = skipSpace(p + keyLen + 2);
    if (*v == ':') return skipSpace(v + 1);
  }
  return nullptr;
}

// Reads an unsigned integer field in [min, max]; returns false if missing or out of range.
bool readUInt(const char *body, const char *key, uint32_t min, uint32_t max, uint32_t &out) {
  const char *v = findValue(body, key);
  if (!v || *v < '0' || *v > '9') return false;
  char *end;
  errno = 0;
  unsigned long value = strtoul(v, &end, 10);
  if (errno || (*end && !strchr(",} \t\r\n", *end)) || value < min || value > max) return false;
  out = static_cast<uint32_t>(value);
  return true;
}

// Copies a string field without escape sequences; returns false if missing, not a string or too long.
bool readString(const char *body, const char *key, char *out, size_t outLen) {
  const char *v = findValue(body, key);
  if (!v || *v != '"') return false;
  v++;
  size_t len = 0;
  for (; v[len] != '"'; len++)
    if (!v[len] || v[len] == '\\' || len + 1 >= outLen) return false;
  memcpy(out, v, len);
  out[len] = '\0';
  return true;
}

typedef void (*JsonHandler)(AsyncWebServerRequest *request, const char *body);

void onJson(AsyncWebServer &server, const char *uri, WebRequestMethodComposite method, JsonHandler handler) {
  server.on(
    uri, method,
    [handler](AsyncWebServerRequest *request) {
      const char *body = static_cast<const char *>(request->_tempObject);
      if (!body) {
        request->send(400, "text/plain", "Expected a JSON body");
        return;
      }
      handler(request, body);
    },
    nullptr, collectBody
  );
}

void handleCreateCharacter(AsyncWebServerRequest *request, const char *body) {
  uint32_t id;
  if (!readUInt(body, "id", 1, 0xFFFF, id)) return request->send(400, "text/plain", "Invalid id");
  char uid[ld::UID_STRING_SIZE] = "";
  sendResult(request, service->createCharacter(static_cast<uint16_t>(id), uid), uid);
}

void handleCreateVehicle(AsyncWebServerRequest *request, const char *body) {
  uint32_t id;
  if (!readUInt(body, "id", 1, 0xFFFF, id)) return request->send(400, "text/plain", "Invalid id");
  char uid[ld::UID_STRING_SIZE] = "";
  sendResult(request, service->createVehicle(static_cast<uint16_t>(id), uid), uid);
}

void handlePlace(AsyncWebServerRequest *request, const char *body) {
  uint32_t position, index;
  char uid[ld::UID_STRING_SIZE];
  if (!readString(body, "uid", uid, sizeof(uid)) || !readUInt(body, "position", 1, 3, position) ||
      !readUInt(body, "index", 1, ld::MAX_PAD_INDEX, index))
    return request->send(400, "text/plain", "Invalid uid, position or index");
  sendResult(request, service->place(uid, static_cast<uint8_t>(position), static_cast<uint8_t>(index)));
}

void handleRemove(AsyncWebServerRequest *request, const char *body) {
  uint32_t index;
  if (!readUInt(body, "index", 1, ld::MAX_PAD_INDEX, index)) return request->send(400, "text/plain", "Invalid index");
  char uid[ld::UID_STRING_SIZE];
  if (!readString(body, "uid", uid, sizeof(uid))) uid[0] = '\0';
  sendResult(request, service->remove(static_cast<uint8_t>(index), uid), "true");
}

void handleDeleteToken(AsyncWebServerRequest *request, const char *body) {
  char uid[ld::UID_STRING_SIZE];
  if (!readString(body, "uid", uid, sizeof(uid))) return request->send(400, "text/plain", "Invalid uid");
  sendResult(request, service->deleteToken(uid));
}

} // namespace

void setupWebApi(AsyncWebServer &server, AsyncEventSource &events, ToyPadService &toyPadService) {
  service = &toyPadService;

  // Real-time updates (replaces Socket.IO). Late joiners get the connection state immediately.
  events.onConnect([](AsyncEventSourceClient *client) {
    if (service->connected()) client->send("null", "Connection True", 0);
  });
  server.addHandler(&events);

  // The toy tags as minified JSON, rendered in chunks from the compact in-memory store
  // (registered before the static handler). The cursor lives in the callback.
  server.on("/json/toytags.json", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginChunkedResponse(
      "application/json", [cursor = ld::TagJsonCursor()](uint8_t *buffer, size_t maxLen, size_t) mutable {
        return service->writeTagsJson(cursor, buffer, maxLen);
      }
    );
    response->addHeader("Cache-Control", "no-cache");
    request->send(response);
  });

  // Create a new character and save it, responds with its UID
  onJson(server, "/character", HTTP_POST, handleCreateCharacter);
  // Create a new vehicle/gadget and save it, responds with its UID
  onJson(server, "/vehicle", HTTP_POST, handleCreateVehicle);
  // Called when a token is placed or moved onto a position of the Toy Pad
  onJson(server, "/place", HTTP_POST, handlePlace);
  // Called when a token needs to be removed from the pad
  onJson(server, "/remove", HTTP_DELETE, handleRemove);
  // Replaces the Socket.IO 'deleteToken' message
  onJson(server, "/token", HTTP_DELETE, handleDeleteToken);

  // Replaces the Socket.IO 'syncToyPad' message
  server.on("/sync", HTTP_POST, [](AsyncWebServerRequest *request) {
    service->sync();
    request->send(200, "text/plain", "");
  });

  // Replaces the Socket.IO 'connectionStatus' message
  server.on("/status", HTTP_GET | HTTP_POST, [&events](AsyncWebServerRequest *request) {
    if (request->method() == HTTP_POST && service->connected()) events.send("null", "Connection True", 0);
    char json[128];
    service->statusJson(json, sizeof(json));
    request->send(200, "application/json", json);
  });

  // The shared index.html also references the Socket.IO client of the Node.js
  // server. Answer with a no-op script so the UI falls back to Server-Sent Events.
  server.on("/socket.io/socket.io.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/javascript", "/* Socket.IO is not used on ESP32-S3, see /events */\n");
  });

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  server.onNotFound([](AsyncWebServerRequest *request) { request->send(404, "text/plain", "Not found"); });
}
