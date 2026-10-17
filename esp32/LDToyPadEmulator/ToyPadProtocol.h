/*
	Copyright © 2023 Berny23, Cort1237 and many more

	This file is part of "Toy Pad Emulator for Lego Dimensions" which is released under the "MIT" license.
	See file "LICENSE" or go to "https://choosealicense.com/licenses/mit" for full license details.
*/

// Low level building blocks of the LEGO Dimensions Toy Pad protocol.
// This is a C++ port of the device emulation subset of node-ld
// (https://github.com/Berny23/node-ld). It has no Arduino/ESP-IDF dependencies
// so it can be unit tested on the host.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ld {

// Every HID report exchanged with the game is exactly 32 bytes.
constexpr size_t REPORT_SIZE = 32;
// Maximum payload of a frame (type + length + payload + checksum = 32).
constexpr size_t MAX_FRAME_PAYLOAD = REPORT_SIZE - 3;
// Size of the emulated NFC tag memory (45 pages of 4 bytes).
constexpr size_t TOKEN_SIZE = 180;
constexpr size_t PAGE_SIZE = 4;
constexpr size_t UID_SIZE = 7;
// Hex string representation of a UID (14 characters + terminator).
constexpr size_t UID_STRING_SIZE = UID_SIZE * 2 + 1;

constexpr uint8_t FRAME_TYPE_MESSAGE = 0x55; // request from game / response from pad
constexpr uint8_t FRAME_TYPE_EVENT = 0x56;   // tag placed/removed notification

enum Command : uint8_t {
  CMD_WAKE = 0xB0,
  CMD_SEED = 0xB1,
  CMD_CHAL = 0xB3,
  CMD_COL = 0xC0,
  CMD_GETCOL = 0xC1,
  CMD_FADE = 0xC2,
  CMD_FLASH = 0xC3,
  CMD_FADRD = 0xC4,
  CMD_FADAL = 0xC6,
  CMD_FLSAL = 0xC7,
  CMD_COLAL = 0xC8,
  CMD_TGLST = 0xD0,
  CMD_READ = 0xD2,
  CMD_WRITE = 0xD3,
  CMD_MODEL = 0xD4,
  CMD_PWD = 0xE1,
  CMD_ACTIVE = 0xE5,
  CMD_LEDSQ = 0xFF,
};

// Builds a 32 byte frame: [type, len, payload..., checksum, 0...].
// Returns false if the payload does not fit.
bool buildFrame(uint8_t type, const uint8_t *payload, size_t len, uint8_t out[REPORT_SIZE]);

// Parsed request sent by the game: 0x55 <len> <cmd> <cid> <payload...> <checksum>
struct Request {
  uint8_t cmd = 0;
  uint8_t cid = 0;
  uint8_t payload[MAX_FRAME_PAYLOAD] = {};
  size_t payloadLen = 0;
};

// Parses a request frame. Returns false if the frame is not a valid request.
bool parseRequest(const uint8_t *data, size_t len, Request &req);

// Tiny Encryption Algorithm as used by the Toy Pad (little endian words).
class Tea {
public:
  explicit Tea(const uint8_t key[16]);
  void encrypt(const uint8_t in[8], uint8_t out[8]) const;
  void decrypt(const uint8_t in[8], uint8_t out[8]) const;

private:
  uint32_t key_[4];
};

// Bob Jenkins' small PRNG used for the challenge/response handshake.
class Burtle {
public:
  void init(uint32_t seed);
  uint32_t rand();

private:
  uint32_t a_ = 0, b_ = 0, c_ = 0, d_ = 0;
};

// Byte helpers (explicit endianness, independent of the host CPU).
uint32_t readU32LE(const uint8_t *p);
uint32_t readU32BE(const uint8_t *p);
uint16_t readU16LE(const uint8_t *p);
void writeU32LE(uint8_t *p, uint32_t v);
void writeU32BE(uint8_t *p, uint32_t v);
void writeU16LE(uint8_t *p, uint16_t v);
void writeU16BE(uint8_t *p, uint16_t v);

// UID helpers. UIDs are exchanged with the web UI as 14 uppercase hex characters.
bool parseUid(const char *hex, uint8_t out[UID_SIZE]);
std::string formatUid(const uint8_t uid[UID_SIZE]);
// Generates a random NXP-like UID (04 xx xx xx xx xx 80) from a random source.
void randomUid(uint32_t (*random32)(), uint8_t out[UID_SIZE]);

// Maps the raw RGB values the game sends to a colour suitable for display in
// the web UI (same lookup table as the Node.js implementation).
std::string displayColor(uint8_t r, uint8_t g, uint8_t b);

} // namespace ld
