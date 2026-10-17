/*
	Copyright © 2023 Berny23, Cort1237 and many more

	This file is part of "Toy Pad Emulator for Lego Dimensions" which is released under the "MIT" license.
	See file "LICENSE" or go to "https://choosealicense.com/licenses/mit" for full license details.
*/

#include "ToyPadEmulator.h"

#include <cstring>

namespace ld {

namespace {
const uint8_t TEA_KEY[16] = {0x55, 0xFE, 0xF6, 0xB0, 0x62, 0xBF, 0x0B, 0x41, 0xC9, 0xB3, 0x7C, 0xB4, 0x97, 0x3E, 0x29, 0x7B};
// "(c) LEGO 2014"
const uint8_t WAKE_RESPONSE[] = {0x28, 0x63, 0x29, 0x20, 0x4c, 0x45, 0x47, 0x4f, 0x20, 0x32, 0x30, 0x31, 0x34};

Rgb rgbAt(const uint8_t *p) {
  Rgb c;
  c.r = p[0];
  c.g = p[1];
  c.b = p[2];
  return c;
}

Fade fadeAt(const uint8_t *p) {
  Fade f;
  f.speed = p[0];
  f.cycles = p[1];
  f.color = rgbAt(p + 2);
  return f;
}
} // namespace

Token makeCharacterToken(uint16_t id, const uint8_t uid[UID_SIZE]) {
  Token t;
  memcpy(t.uid, uid, UID_SIZE);
  t.characterId = id;
  return t;
}

Token makeVehicleToken(uint16_t id, const uint8_t uid[UID_SIZE], uint32_t upgradesP23, uint32_t upgradesP25) {
  Token t;
  memcpy(t.uid, uid, UID_SIZE);
  writeU32LE(t.data + 0x23 * PAGE_SIZE, upgradesP23);
  writeU16LE(t.data + 0x24 * PAGE_SIZE, id);
  writeU32LE(t.data + 0x25 * PAGE_SIZE, upgradesP25);
  writeU16BE(t.data + 0x26 * PAGE_SIZE, 1); // page 0x26 is used for verification of some kind
  return t;
}

ToyPadEmulator::ToyPadEmulator() : tea_(TEA_KEY) { memset(events_, 0, sizeof(events_)); }

bool ToyPadEmulator::place(const Token &token, uint8_t pad, uint8_t index) {
  if (pad < 1 || pad > 3 || index < 1 || index > MAX_PAD_INDEX) return false;
  if (tokens_[index].present) return false;
  if (tokensOnPad(pad) >= (pad == 1 ? 1u : 3u)) return false;
  Token &slot = tokens_[index];
  slot = token;
  slot.present = true;
  slot.pad = pad;
  slot.index = index;
  queueTagEvent(slot, false);
  return true;
}

bool ToyPadEmulator::remove(uint8_t index) {
  if (index < 1 || index > MAX_PAD_INDEX || !tokens_[index].present) return false;
  queueTagEvent(tokens_[index], true);
  tokens_[index] = Token();
  return true;
}

void ToyPadEmulator::clear() {
  for (auto &t : tokens_) t = Token();
  eventHead_ = eventCount_ = 0;
}

const Token *ToyPadEmulator::tokenAt(uint8_t index) const {
  if (index < 1 || index > MAX_PAD_INDEX || !tokens_[index].present) return nullptr;
  return &tokens_[index];
}

size_t ToyPadEmulator::tokensOnPad(uint8_t pad) const {
  size_t n = 0;
  for (uint8_t i = 1; i <= MAX_PAD_INDEX; i++)
    if (tokens_[i].present && tokens_[i].pad == pad) n++;
  return n;
}

void ToyPadEmulator::queueTagEvent(const Token &token, bool removed) {
  uint8_t payload[4 + UID_SIZE];
  payload[0] = token.pad;
  payload[1] = 0;
  payload[2] = token.index;
  payload[3] = removed ? 1 : 0;
  memcpy(payload + 4, token.uid, UID_SIZE);

  if (eventCount_ == EVENT_QUEUE_SIZE) { // drop the oldest event
    eventHead_ = (eventHead_ + 1) % EVENT_QUEUE_SIZE;
    eventCount_--;
  }
  size_t tail = (eventHead_ + eventCount_) % EVENT_QUEUE_SIZE;
  buildFrame(FRAME_TYPE_EVENT, payload, sizeof(payload), events_[tail]);
  eventCount_++;
}

bool ToyPadEmulator::peekEvent(uint8_t out[REPORT_SIZE]) const {
  if (!eventCount_) return false;
  memcpy(out, events_[eventHead_], REPORT_SIZE);
  return true;
}

bool ToyPadEmulator::popEvent(uint8_t out[REPORT_SIZE]) {
  if (!peekEvent(out)) return false;
  eventHead_ = (eventHead_ + 1) % EVENT_QUEUE_SIZE;
  eventCount_--;
  return true;
}

bool ToyPadEmulator::handleReport(const uint8_t *data, size_t len, uint8_t response[REPORT_SIZE]) {
  Request req;
  if (!parseRequest(data, len, req)) return false;

  // Response payload: [cid, payload...]
  uint8_t payload[MAX_FRAME_PAYLOAD] = {};
  size_t payloadLen = 0;
  handleRequest(req, payload + 1, payloadLen);
  payload[0] = req.cid;
  return buildFrame(FRAME_TYPE_MESSAGE, payload, payloadLen + 1, response);
}

void ToyPadEmulator::handleRequest(const Request &req, uint8_t *out, size_t &outLen) {
  const uint8_t *p = req.payload;
  outLen = 0;

  switch (req.cmd) {
  case CMD_WAKE:
    memcpy(out, WAKE_RESPONSE, sizeof(WAKE_RESPONSE));
    outLen = sizeof(WAKE_RESPONSE);
    // The game (re)initialised the pad: events queued before are obsolete.
    // Re-announce every token that is already on the pad (highest index first, like node-ld).
    eventHead_ = eventCount_ = 0;
    for (int i = MAX_PAD_INDEX; i >= 1; i--)
      if (tokens_[i].present) queueTagEvent(tokens_[i], false);
    connected_ = true;
    if (listener_) listener_->onWake();
    break;

  case CMD_SEED: {
    uint8_t d[8];
    tea_.decrypt(p, d);
    uint32_t seed = readU32LE(d);
    uint32_t conf = readU32BE(d + 4);
    burtle_.init(seed);
    uint8_t r[8] = {};
    writeU32BE(r, conf);
    tea_.encrypt(r, out);
    outLen = 8;
    break;
  }

  case CMD_CHAL: {
    uint8_t d[8];
    tea_.decrypt(p, d);
    uint32_t conf = readU32BE(d);
    uint8_t r[8];
    writeU32LE(r, burtle_.rand());
    writeU32BE(r + 4, conf);
    tea_.encrypt(r, out);
    outLen = 8;
    break;
  }

  case CMD_MODEL: {
    uint8_t d[8];
    tea_.decrypt(p, d);
    uint8_t index = d[0];
    uint32_t conf = readU32BE(d + 4);
    uint8_t r[8] = {};
    writeU32BE(r + 4, conf);
    const Token *token = tokenAt(index);
    out[0] = 0;
    if (!token)
      out[0] = 0xF2; // no tag at this index
    else if (token->characterId)
      writeU32LE(r, token->characterId);
    else
      out[0] = 0xF9; // not a character (vehicle/gadget)
    tea_.encrypt(r, out + 1);
    outLen = 9;
    break;
  }

  case CMD_READ: {
    uint8_t index = p[0];
    size_t start = static_cast<size_t>(p[1]) * PAGE_SIZE;
    memset(out, 0, 17);
    const Token *token = tokenAt(index);
    if (token && start < TOKEN_SIZE) {
      size_t n = TOKEN_SIZE - start < 16 ? TOKEN_SIZE - start : 16;
      memcpy(out + 1, token->data + start, n);
    }
    outLen = 17;
    break;
  }

  case CMD_WRITE: {
    uint8_t index = p[0];
    uint8_t page = p[1];
    out[0] = 0;
    outLen = 1;
    if (index >= 1 && index <= MAX_PAD_INDEX && tokens_[index].present) {
      Token &token = tokens_[index];
      size_t start = static_cast<size_t>(page) * PAGE_SIZE;
      if (start + PAGE_SIZE <= TOKEN_SIZE) memcpy(token.data + start, p + 2, PAGE_SIZE);
      if (listener_) listener_->onTagWrite(token, page, p + 2);
    }
    break;
  }

  case CMD_COL:
    if (listener_) listener_->onColor(p[0], rgbAt(p + 1));
    break;

  case CMD_FADE:
    if (listener_) listener_->onFade(p[0], fadeAt(p + 1));
    break;

  case CMD_FADAL:
    if (listener_) {
      const Fade fades[3] = {fadeAt(p + 1), fadeAt(p + 7), fadeAt(p + 13)};
      listener_->onFadeAll(fades);
    }
    break;

  case CMD_COLAL:
    if (listener_) {
      const Rgb colors[3] = {rgbAt(p + 1), rgbAt(p + 5), rgbAt(p + 9)};
      listener_->onColorAll(colors);
    }
    break;

  default:
    // CMD_GETCOL, CMD_FLASH, CMD_FADRD, CMD_FLSAL, CMD_TGLST, CMD_PWD, CMD_ACTIVE, CMD_LEDSQ, ...
    // are acknowledged with an empty payload, just like the Node.js implementation.
    break;
  }
}

} // namespace ld
