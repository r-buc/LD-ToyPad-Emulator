/*
	Copyright © 2023 Berny23, Cort1237 and many more

	This file is part of "Toy Pad Emulator for Lego Dimensions" which is released under the "MIT" license.
	See file "LICENSE" or go to "https://choosealicense.com/licenses/mit" for full license details.
*/

// Device side state machine of the Toy Pad (port of node-ld's ToyPadEmu plus
// the command hooks that index.js registers). The emulator is transport
// agnostic: feed it the 32 byte reports received from the game and send the
// produced reports back. Not thread safe, callers must serialise access.

#pragma once

#include "ToyPadProtocol.h"

#include <cstddef>
#include <cstdint>

namespace ld {

constexpr uint8_t MAX_PAD_INDEX = 7; // the UI exposes 7 positions (1..7)

struct Token {
  bool present = false;
  uint8_t index = 0; // position on the pad (1..7)
  uint8_t pad = 0;   // 1 = centre, 2 = left, 3 = right
  uint8_t uid[UID_SIZE] = {};
  uint8_t data[TOKEN_SIZE] = {};
  // Non-zero for characters: returned (encrypted) on CMD_MODEL. Vehicles keep
  // their id in page 0x24 of the tag memory instead.
  uint16_t characterId = 0;
};

struct Rgb {
  uint8_t r = 0, g = 0, b = 0;
};

struct Fade {
  uint8_t speed = 0;
  uint8_t cycles = 0;
  Rgb color;
};

// Receives the side effects of game commands (lights, tag writes, wake up).
class ToyPadListener {
public:
  virtual ~ToyPadListener() = default;
  virtual void onWake() {}
  // pad == 0 means all pads.
  virtual void onColor(uint8_t /*pad*/, const Rgb & /*color*/) {}
  // colors[0] = centre, [1] = left, [2] = right
  virtual void onColorAll(const Rgb /*colors*/[3]) {}
  virtual void onFade(uint8_t /*pad*/, const Fade & /*fade*/) {}
  virtual void onFadeAll(const Fade /*fades*/[3]) {}
  // The game wrote 4 bytes to `page` of the tag currently at token.index.
  virtual void onTagWrite(const Token & /*token*/, uint8_t /*page*/, const uint8_t /*data*/[PAGE_SIZE]) {}
};

// Creates the tag memory of a character (only the id is relevant, answered via CMD_MODEL).
Token makeCharacterToken(uint16_t id, const uint8_t uid[UID_SIZE]);
// Creates the tag memory of a vehicle/gadget including its upgrade pages.
Token makeVehicleToken(uint16_t id, const uint8_t uid[UID_SIZE], uint32_t upgradesP23, uint32_t upgradesP25);

class ToyPadEmulator {
public:
  static constexpr size_t EVENT_QUEUE_SIZE = 16;

  ToyPadEmulator();

  void setListener(ToyPadListener *listener) { listener_ = listener; }

  // Processes one report received from the game. Returns true and fills
  // `response` if a reply must be sent back.
  bool handleReport(const uint8_t *data, size_t len, uint8_t response[REPORT_SIZE]);

  // Places a token on the pad. Fails if the pad/index is invalid, the index is
  // already occupied or the pad is full (centre: 1, sides: 3 tokens).
  bool place(const Token &token, uint8_t pad, uint8_t index);
  // Removes the token at `index`. Returns false if there is none.
  bool remove(uint8_t index);
  void clear();

  // Returns the token at `index` or nullptr.
  const Token *tokenAt(uint8_t index) const;
  size_t tokensOnPad(uint8_t pad) const;

  // Pending tag place/remove events (32 byte frames of type 0x56).
  bool hasEvent() const { return eventCount_ > 0; }
  bool peekEvent(uint8_t out[REPORT_SIZE]) const;
  bool popEvent(uint8_t out[REPORT_SIZE]);

  bool connected() const { return connected_; }
  void setConnected(bool connected) { connected_ = connected; }

private:
  void queueTagEvent(const Token &token, bool removed);
  void handleRequest(const Request &req, uint8_t *payload, size_t &payloadLen);

  Token tokens_[MAX_PAD_INDEX + 1];
  uint8_t events_[EVENT_QUEUE_SIZE][REPORT_SIZE];
  size_t eventHead_ = 0;
  size_t eventCount_ = 0;
  Tea tea_;
  Burtle burtle_;
  ToyPadListener *listener_ = nullptr;
  bool connected_ = false;
};

} // namespace ld
