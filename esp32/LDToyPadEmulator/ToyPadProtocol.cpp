/*
	Copyright © 2023 Berny23, Cort1237 and many more

	This file is part of "Toy Pad Emulator for Lego Dimensions" which is released under the "MIT" license.
	See file "LICENSE" or go to "https://choosealicense.com/licenses/mit" for full license details.
*/

#include "ToyPadProtocol.h"

#include <cstdio>
#include <cstring>

namespace ld {

bool buildFrame(uint8_t type, const uint8_t *payload, size_t len, uint8_t out[REPORT_SIZE]) {
  if (len > MAX_FRAME_PAYLOAD) return false;
  memset(out, 0, REPORT_SIZE);
  out[0] = type;
  out[1] = static_cast<uint8_t>(len);
  if (len) memcpy(out + 2, payload, len);
  uint8_t sum = 0;
  for (size_t i = 0; i < len + 2; i++) sum += out[i];
  out[len + 2] = sum;
  return true;
}

bool parseRequest(const uint8_t *data, size_t len, Request &req) {
  if (!data || len < 4 || data[0] != FRAME_TYPE_MESSAGE) return false;
  size_t frameLen = data[1];
  if (frameLen < 2 || frameLen + 2 > len || frameLen > MAX_FRAME_PAYLOAD) return false;
  req.cmd = data[2];
  req.cid = data[3];
  req.payloadLen = frameLen - 2;
  memset(req.payload, 0, sizeof(req.payload));
  memcpy(req.payload, data + 4, req.payloadLen);
  return true;
}

uint32_t readU32LE(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

uint32_t readU32BE(const uint8_t *p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) |
         static_cast<uint32_t>(p[3]);
}

uint16_t readU16LE(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

void writeU32LE(uint8_t *p, uint32_t v) {
  p[0] = v & 0xFF;
  p[1] = (v >> 8) & 0xFF;
  p[2] = (v >> 16) & 0xFF;
  p[3] = (v >> 24) & 0xFF;
}

void writeU32BE(uint8_t *p, uint32_t v) {
  p[0] = (v >> 24) & 0xFF;
  p[1] = (v >> 16) & 0xFF;
  p[2] = (v >> 8) & 0xFF;
  p[3] = v & 0xFF;
}

void writeU16LE(uint8_t *p, uint16_t v) {
  p[0] = v & 0xFF;
  p[1] = (v >> 8) & 0xFF;
}

void writeU16BE(uint8_t *p, uint16_t v) {
  p[0] = (v >> 8) & 0xFF;
  p[1] = v & 0xFF;
}

Tea::Tea(const uint8_t key[16]) {
  for (int i = 0; i < 4; i++) key_[i] = readU32LE(key + i * 4);
}

void Tea::encrypt(const uint8_t in[8], uint8_t out[8]) const {
  uint32_t v0 = readU32LE(in), v1 = readU32LE(in + 4), sum = 0;
  const uint32_t delta = 0x9E3779B9;
  for (int i = 0; i < 32; i++) {
    sum += delta;
    v0 += ((v1 << 4) + key_[0]) ^ (v1 + sum) ^ ((v1 >> 5) + key_[1]);
    v1 += ((v0 << 4) + key_[2]) ^ (v0 + sum) ^ ((v0 >> 5) + key_[3]);
  }
  writeU32LE(out, v0);
  writeU32LE(out + 4, v1);
}

void Tea::decrypt(const uint8_t in[8], uint8_t out[8]) const {
  uint32_t v0 = readU32LE(in), v1 = readU32LE(in + 4), sum = 0xC6EF3720;
  const uint32_t delta = 0x9E3779B9;
  for (int i = 0; i < 32; i++) {
    v1 -= ((v0 << 4) + key_[2]) ^ (v0 + sum) ^ ((v0 >> 5) + key_[3]);
    v0 -= ((v1 << 4) + key_[0]) ^ (v1 + sum) ^ ((v1 >> 5) + key_[1]);
    sum -= delta;
  }
  writeU32LE(out, v0);
  writeU32LE(out + 4, v1);
}

static inline uint32_t rotl32(uint32_t a, unsigned b) { return (a << b) | (a >> (32 - b)); }

void Burtle::init(uint32_t seed) {
  a_ = 0xf1ea5eed;
  b_ = c_ = d_ = seed;
  for (int i = 0; i < 42; i++) rand();
}

uint32_t Burtle::rand() {
  uint32_t e = a_ - rotl32(b_, 21);
  a_ = b_ ^ rotl32(c_, 19);
  b_ = c_ + rotl32(d_, 6);
  c_ = d_ + e;
  d_ = e + a_;
  return d_;
}

static int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool parseUid(const char *hex, uint8_t out[UID_SIZE]) {
  if (!hex || strlen(hex) != UID_SIZE * 2) return false;
  for (size_t i = 0; i < UID_SIZE; i++) {
    int hi = hexValue(hex[i * 2]);
    int lo = hexValue(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

std::string formatUid(const uint8_t uid[UID_SIZE]) {
  char buf[UID_STRING_SIZE];
  for (size_t i = 0; i < UID_SIZE; i++) snprintf(buf + i * 2, 3, "%02X", uid[i]);
  return std::string(buf);
}

void randomUid(uint32_t (*random32)(), uint8_t uid[UID_SIZE]) {
  uid[0] = 0x04; // vendor id 04 = NXP
  uid[6] = 0x80; // for whatever reason the last byte of the UID is mostly 0x8*
  for (size_t i = 1; i < 6; i++) uid[i] = static_cast<uint8_t>(random32() & 0xFF);
}

std::string displayColor(uint8_t r, uint8_t g, uint8_t b) {
  struct Mapping {
    uint32_t raw;
    uint32_t display;
  };
  static const Mapping mappings[] = {
    {0x99420e, 0xffffff}, // idle (full white)
    // rainbow sequence (title screen, some are used by keystones)
    {0xff6e00, 0xffff00}, // yellow
    {0x006e00, 0x00ff00}, // green
    {0x006e18, 0x00ffff}, // cyan
    {0x000018, 0x0000ff}, // blue
    {0xff0018, 0xff00ff}, // pink
    {0xf00016, 0xff2de6}, // wyldstyle scanner
    // shift keystone (dark colors for blink animation)
    {0x002007, 0x007575},
    {0x4c2000, 0x757500},
    {0x4c0007, 0x750075},
    // chroma keystone
    {0x3f1b05, 0xb0b0b0},
    {0x4c2007, 0x757575},
    {0x3f1b00, 0xb0b000},
    {0x3f0000, 0xb00000},
    {0x000005, 0x0000b0},
    {0x001b00, 0x00b000},
    {0xff2700, 0xffa200},
    {0x3f0900, 0xb06f00},
    {0x44000d, 0xd500ff},
    {0x110003, 0x9300b0},
    // element keystone
    {0x000016, 0x0000ff},
    {0x006700, 0x00ff00},
    // scale keystone
    {0xff1e00, 0xffa200},
    {0xf06716, 0xffffff},
    // green (hack minigame)
    {0x003700, 0x00ff00},
    // other
    {0xff6e18, 0xffffff},
  };

  uint32_t color = (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
  for (const auto &m : mappings) {
    if (m.raw == color) {
      color = m.display;
      break;
    }
  }
  char buf[8];
  snprintf(buf, sizeof(buf), "#%06x", static_cast<unsigned>(color));
  return std::string(buf);
}

} // namespace ld
