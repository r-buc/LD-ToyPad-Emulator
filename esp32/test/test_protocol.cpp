// Host side unit tests for the Toy Pad protocol port (make -C esp32/test).
// Expected values were generated with node-ld (https://github.com/Berny23/node-ld),
// the library used by the Node.js version of the emulator.

#include "ToyPadEmulator.h"
#include "ToyPadProtocol.h"
#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

using namespace ld;

namespace {

std::vector<uint8_t> fromHex(const std::string &hex) {
  std::vector<uint8_t> out;
  for (size_t i = 0; i + 1 < hex.size(); i += 2) out.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
  return out;
}

std::string toHex(const uint8_t *data, size_t len) {
  static const char *digits = "0123456789abcdef";
  std::string s;
  for (size_t i = 0; i < len; i++) {
    s += digits[data[i] >> 4];
    s += digits[data[i] & 0xF];
  }
  return s;
}

const uint8_t TEA_KEY[16] = {0x55, 0xFE, 0xF6, 0xB0, 0x62, 0xBF, 0x0B, 0x41, 0xC9, 0xB3, 0x7C, 0xB4, 0x97, 0x3E, 0x29, 0x7B};
const uint8_t TEST_UID[UID_SIZE] = {0x04, 0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0x80};

std::string color(const Rgb &c) {
  char out[COLOR_STRING_SIZE];
  displayColor(c.r, c.g, c.b, out);
  return out;
}

std::string color(uint8_t r, uint8_t g, uint8_t b) { return color(Rgb{r, g, b}); }

std::string respond(ToyPadEmulator &emu, const std::string &requestHex) {
  auto req = fromHex(requestHex);
  uint8_t out[REPORT_SIZE];
  TEST_ASSERT_TRUE(emu.handleReport(req.data(), req.size(), out));
  return toHex(out, REPORT_SIZE);
}

struct RecordingListener : ToyPadListener {
  int wakes = 0;
  int colorPad = -1;
  Rgb color;
  Rgb colorsAll[3];
  int fadePad = -1;
  Fade fade;
  Fade fadesAll[3];
  int writes = 0;
  uint8_t lastPage = 0;
  uint32_t lastData = 0;

  void onWake() override { wakes++; }
  void onColor(uint8_t pad, const Rgb &c) override {
    colorPad = pad;
    color = c;
  }
  void onColorAll(const Rgb c[3]) override {
    for (int i = 0; i < 3; i++) colorsAll[i] = c[i];
  }
  void onFade(uint8_t pad, const Fade &f) override {
    fadePad = pad;
    fade = f;
  }
  void onFadeAll(const Fade f[3]) override {
    for (int i = 0; i < 3; i++) fadesAll[i] = f[i];
  }
  void onTagWrite(const Token &, uint8_t page, const uint8_t data[PAGE_SIZE]) override {
    writes++;
    lastPage = page;
    lastData = readU32LE(data);
  }
};

} // namespace

void setUp() {}
void tearDown() {}

void test_tea_matches_node_ld() {
  Tea tea(TEA_KEY);
  auto plain = fromHex("0123456789abcdef");
  uint8_t out[8];
  tea.encrypt(plain.data(), out);
  TEST_ASSERT_EQUAL_STRING("e8f66c96a642d580", toHex(out, 8).c_str());
  tea.decrypt(plain.data(), out);
  TEST_ASSERT_EQUAL_STRING("273d555dec3588e2", toHex(out, 8).c_str());

  uint8_t back[8];
  tea.encrypt(out, back);
  TEST_ASSERT_EQUAL_MEMORY(plain.data(), back, 8);
}

void test_burtle_matches_node_ld() {
  Burtle b;
  b.init(0x12345678);
  TEST_ASSERT_EQUAL_HEX32(0x154f6e1b, b.rand());
  TEST_ASSERT_EQUAL_HEX32(0xeb391089, b.rand());
  TEST_ASSERT_EQUAL_HEX32(0x2991a345, b.rand());
}

void test_frame_checksum() {
  auto payload = fromHex("01286329204c45474f2032303134");
  uint8_t out[REPORT_SIZE];
  TEST_ASSERT_TRUE(buildFrame(FRAME_TYPE_MESSAGE, payload.data(), payload.size(), out));
  TEST_ASSERT_EQUAL_STRING("550e01286329204c45474f203230313446000000000000000000000000000000", toHex(out, REPORT_SIZE).c_str());

  uint8_t big[MAX_FRAME_PAYLOAD + 1] = {};
  TEST_ASSERT_FALSE(buildFrame(FRAME_TYPE_MESSAGE, big, sizeof(big), out));
}

void test_parse_request_rejects_garbage() {
  Request req;
  uint8_t bad[REPORT_SIZE] = {0x56, 0x02, 0xB0, 0x01};
  TEST_ASSERT_FALSE(parseRequest(bad, sizeof(bad), req));
  uint8_t tooLong[REPORT_SIZE] = {0x55, 0x40, 0xB0, 0x01};
  TEST_ASSERT_FALSE(parseRequest(tooLong, sizeof(tooLong), req));
  TEST_ASSERT_FALSE(parseRequest(nullptr, 0, req));

  ToyPadEmulator emu;
  uint8_t out[REPORT_SIZE];
  TEST_ASSERT_FALSE(emu.handleReport(bad, sizeof(bad), out));
}

void test_wake_response_and_listener() {
  ToyPadEmulator emu;
  RecordingListener listener;
  emu.setListener(&listener);
  TEST_ASSERT_FALSE(emu.connected());
  // 55 0f b0 01 "(c) LEGO 2014" + checksum, as sent by the game
  std::string res = respond(emu, "550fb001286329204c45474f2032303134f7");
  TEST_ASSERT_EQUAL_STRING("550e01286329204c45474f203230313446000000000000000000000000000000", res.c_str());
  TEST_ASSERT_TRUE(emu.connected());
  TEST_ASSERT_EQUAL(1, listener.wakes);
}

void test_seed_challenge_handshake() {
  ToyPadEmulator emu;
  TEST_ASSERT_EQUAL_STRING("550905cc8d1bad02a44d6fe60000000000000000000000000000000000000000",
                           respond(emu, "550ab1054165f916b37578ae1800000000000000000000000000000000000000").c_str());
  TEST_ASSERT_EQUAL_STRING("5509068d4f9d20840c736e6e0000000000000000000000000000000000000000",
                           respond(emu, "550ab3067fa6026a925f0e19c100000000000000000000000000000000000000").c_str());
}

void test_model_request() {
  const std::string modelReq = "550ad407f05c9190130a210ff400000000000000000000000000000000000000";
  ToyPadEmulator emu;
  // nothing at index 1
  TEST_ASSERT_EQUAL_STRING("550a07f29881b8f46bf0309f4700000000000000000000000000000000000000", respond(emu, modelReq).c_str());

  // character (Batman, id 1) at index 1
  TEST_ASSERT_TRUE(emu.place(makeCharacterToken(1, TEST_UID), 2, 1));
  TEST_ASSERT_EQUAL_STRING("550a0700f05c9190130a210f2000000000000000000000000000000000000000", respond(emu, modelReq).c_str());

  // vehicle at index 1
  TEST_ASSERT_TRUE(emu.remove(1));
  TEST_ASSERT_TRUE(emu.place(makeVehicleToken(1000, TEST_UID, 0, 0), 2, 1));
  TEST_ASSERT_EQUAL_STRING("550a07f99881b8f46bf0309f4e00000000000000000000000000000000000000", respond(emu, modelReq).c_str());
}

void test_place_and_remove_events() {
  ToyPadEmulator emu;
  uint8_t ev[REPORT_SIZE];
  TEST_ASSERT_FALSE(emu.popEvent(ev));

  TEST_ASSERT_TRUE(emu.place(makeCharacterToken(1, TEST_UID), 2, 4));
  TEST_ASSERT_TRUE(emu.popEvent(ev));
  TEST_ASSERT_EQUAL_STRING("560b0200040004a1b2c3d4e580ba000000000000000000000000000000000000", toHex(ev, REPORT_SIZE).c_str());

  TEST_ASSERT_TRUE(emu.remove(4));
  TEST_ASSERT_TRUE(emu.popEvent(ev));
  TEST_ASSERT_EQUAL_HEX8(0x01, ev[5]); // direction: removed
  TEST_ASSERT_FALSE(emu.popEvent(ev));
  TEST_ASSERT_FALSE(emu.remove(4));
}

void test_place_limits() {
  ToyPadEmulator emu;
  TEST_ASSERT_TRUE(emu.place(makeCharacterToken(1, TEST_UID), 1, 2));
  TEST_ASSERT_FALSE(emu.place(makeCharacterToken(2, TEST_UID), 1, 3)); // centre pad holds one token
  TEST_ASSERT_FALSE(emu.place(makeCharacterToken(2, TEST_UID), 2, 2)); // index occupied
  TEST_ASSERT_FALSE(emu.place(makeCharacterToken(2, TEST_UID), 4, 3)); // invalid pad
  TEST_ASSERT_FALSE(emu.place(makeCharacterToken(2, TEST_UID), 2, 8)); // invalid index
  TEST_ASSERT_TRUE(emu.place(makeCharacterToken(2, TEST_UID), 2, 1));
  TEST_ASSERT_TRUE(emu.place(makeCharacterToken(3, TEST_UID), 2, 4));
  TEST_ASSERT_TRUE(emu.place(makeCharacterToken(4, TEST_UID), 2, 5));
  TEST_ASSERT_FALSE(emu.place(makeCharacterToken(5, TEST_UID), 2, 6)); // side pad holds three tokens
  TEST_ASSERT_EQUAL(3, emu.tokensOnPad(2));
}

void test_index_of() {
  ToyPadEmulator emu;
  const uint8_t other[UID_SIZE] = {0x04, 1, 2, 3, 4, 5, 0x80};
  TEST_ASSERT_EQUAL(-1, emu.indexOf(TEST_UID));
  TEST_ASSERT_TRUE(emu.place(makeCharacterToken(1, other), 2, 1));
  TEST_ASSERT_TRUE(emu.place(makeCharacterToken(2, TEST_UID), 3, 7));
  TEST_ASSERT_EQUAL(7, emu.indexOf(TEST_UID));
  TEST_ASSERT_EQUAL(1, emu.indexOf(other));
  TEST_ASSERT_TRUE(emu.remove(7));
  TEST_ASSERT_EQUAL(-1, emu.indexOf(TEST_UID));
  TEST_ASSERT_NULL(emu.tokenAt(0));
  TEST_ASSERT_NULL(emu.tokenAt(MAX_PAD_INDEX + 1));
}

void test_wake_reannounces_tokens() {
  ToyPadEmulator emu;
  TEST_ASSERT_TRUE(emu.place(makeCharacterToken(1, TEST_UID), 2, 1));
  TEST_ASSERT_TRUE(emu.place(makeCharacterToken(2, TEST_UID), 3, 3));
  uint8_t ev[REPORT_SIZE];
  TEST_ASSERT_TRUE(emu.peekEvent(ev));
  TEST_ASSERT_EQUAL(1, ev[4]);
  // Events still queued when the game wakes the pad are replaced by a fresh announcement
  respond(emu, "550fb001286329204c45474f2032303134f7");
  TEST_ASSERT_TRUE(emu.popEvent(ev));
  TEST_ASSERT_EQUAL(3, ev[4]); // highest index first
  TEST_ASSERT_TRUE(emu.popEvent(ev));
  TEST_ASSERT_EQUAL(1, ev[4]);
  TEST_ASSERT_FALSE(emu.popEvent(ev));
}

void test_vehicle_read_and_write() {
  ToyPadEmulator emu;
  RecordingListener listener;
  emu.setListener(&listener);
  TEST_ASSERT_TRUE(emu.place(makeVehicleToken(0x03f0, TEST_UID, 0xefffffff, 0x12345678), 3, 6));

  // CMD_READ index 6, page 0x24 -> id (LE), page 0x25 upgrades, page 0x26 = 00 01
  uint8_t req[REPORT_SIZE];
  uint8_t readPayload[] = {CMD_READ, 0x09, 6, 0x24};
  buildFrame(FRAME_TYPE_MESSAGE, readPayload, sizeof(readPayload), req);
  uint8_t out[REPORT_SIZE];
  TEST_ASSERT_TRUE(emu.handleReport(req, sizeof(req), out));
  TEST_ASSERT_EQUAL_HEX8(0x55, out[0]);
  TEST_ASSERT_EQUAL(18, out[1]);
  TEST_ASSERT_EQUAL_HEX8(0x09, out[2]); // cid
  TEST_ASSERT_EQUAL_HEX8(0x00, out[3]); // status
  TEST_ASSERT_EQUAL_STRING("f0030000785634120001000000000000", toHex(out + 4, 16).c_str());

  // Reading past the end of the tag memory returns zeros
  uint8_t readEnd[] = {CMD_READ, 0x0a, 6, 0x2c};
  buildFrame(FRAME_TYPE_MESSAGE, readEnd, sizeof(readEnd), req);
  TEST_ASSERT_TRUE(emu.handleReport(req, sizeof(req), out));
  TEST_ASSERT_EQUAL_STRING("00000000000000000000000000000000", toHex(out + 4, 16).c_str());

  // CMD_WRITE index 6, page 0x23 -> stored in the token and reported to the listener
  uint8_t writePayload[] = {CMD_WRITE, 0x0b, 6, 0x23, 0x11, 0x22, 0x33, 0x44};
  buildFrame(FRAME_TYPE_MESSAGE, writePayload, sizeof(writePayload), req);
  TEST_ASSERT_TRUE(emu.handleReport(req, sizeof(req), out));
  TEST_ASSERT_EQUAL(2, out[1]);
  TEST_ASSERT_EQUAL_HEX8(0x0b, out[2]);
  TEST_ASSERT_EQUAL_HEX8(0x00, out[3]);
  TEST_ASSERT_EQUAL(1, listener.writes);
  TEST_ASSERT_EQUAL(0x23, listener.lastPage);
  TEST_ASSERT_EQUAL_HEX32(0x44332211, listener.lastData);
  TEST_ASSERT_EQUAL_HEX32(0x44332211, readU32LE(emu.tokenAt(6)->data + 0x23 * 4));

  // Writes to an empty index are acknowledged but ignored
  writePayload[2] = 7;
  buildFrame(FRAME_TYPE_MESSAGE, writePayload, sizeof(writePayload), req);
  TEST_ASSERT_TRUE(emu.handleReport(req, sizeof(req), out));
  TEST_ASSERT_EQUAL(1, listener.writes);
}

void test_light_commands() {
  ToyPadEmulator emu;
  RecordingListener listener;
  emu.setListener(&listener);
  uint8_t req[REPORT_SIZE];
  uint8_t out[REPORT_SIZE];

  uint8_t col[] = {CMD_COL, 0x01, 2, 0xff, 0x6e, 0x00};
  buildFrame(FRAME_TYPE_MESSAGE, col, sizeof(col), req);
  TEST_ASSERT_TRUE(emu.handleReport(req, sizeof(req), out));
  TEST_ASSERT_EQUAL(1, out[1]); // empty acknowledgement (cid only)
  TEST_ASSERT_EQUAL(2, listener.colorPad);
  TEST_ASSERT_EQUAL_STRING("#ffff00", color(listener.color).c_str());

  uint8_t fade[] = {CMD_FADE, 0x02, 3, 10, 2, 0x12, 0x34, 0x56};
  buildFrame(FRAME_TYPE_MESSAGE, fade, sizeof(fade), req);
  TEST_ASSERT_TRUE(emu.handleReport(req, sizeof(req), out));
  TEST_ASSERT_EQUAL(3, listener.fadePad);
  TEST_ASSERT_EQUAL(10, listener.fade.speed);
  TEST_ASSERT_EQUAL(2, listener.fade.cycles);
  TEST_ASSERT_EQUAL_STRING("#123456", color(listener.fade.color).c_str());

  uint8_t colal[] = {CMD_COLAL, 0x03, 1, 1, 2, 3, 1, 4, 5, 6, 1, 7, 8, 9};
  buildFrame(FRAME_TYPE_MESSAGE, colal, sizeof(colal), req);
  TEST_ASSERT_TRUE(emu.handleReport(req, sizeof(req), out));
  TEST_ASSERT_EQUAL(1, listener.colorsAll[0].r);
  TEST_ASSERT_EQUAL(5, listener.colorsAll[1].g);
  TEST_ASSERT_EQUAL(9, listener.colorsAll[2].b);

  uint8_t fadal[] = {CMD_FADAL, 0x04, 1, 1, 1, 1, 2, 3, 1, 2, 2, 4, 5, 6, 1, 3, 3, 7, 8, 9};
  buildFrame(FRAME_TYPE_MESSAGE, fadal, sizeof(fadal), req);
  TEST_ASSERT_TRUE(emu.handleReport(req, sizeof(req), out));
  TEST_ASSERT_EQUAL(1, listener.fadesAll[0].speed);
  TEST_ASSERT_EQUAL(2, listener.fadesAll[1].cycles);
  TEST_ASSERT_EQUAL(9, listener.fadesAll[2].color.b);
}

void test_display_color_mapping() {
  TEST_ASSERT_EQUAL_STRING("#ffffff", color(0x99, 0x42, 0x0e).c_str());
  TEST_ASSERT_EQUAL_STRING("#ff0000", color(0xff, 0x00, 0x00).c_str());
  TEST_ASSERT_EQUAL_STRING("#000000", color(0, 0, 0).c_str());
}

void test_uid_helpers() {
  uint8_t uid[UID_SIZE];
  TEST_ASSERT_TRUE(parseUid("04a1B2c3d4e580", uid));
  TEST_ASSERT_EQUAL_MEMORY(TEST_UID, uid, UID_SIZE);
  char text[UID_STRING_SIZE];
  formatUid(uid, text);
  TEST_ASSERT_EQUAL_STRING("04A1B2C3D4E580", text);
  TEST_ASSERT_FALSE(parseUid("04A1B2C3D4E5", uid));
  TEST_ASSERT_FALSE(parseUid("04A1B2C3D4E5ZZ", uid));
  TEST_ASSERT_FALSE(parseUid(nullptr, uid));

  randomUid([]() -> uint32_t { return 0xAB; }, uid);
  formatUid(uid, text);
  TEST_ASSERT_EQUAL_STRING("04ABABABABAB80", text);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_tea_matches_node_ld);
  RUN_TEST(test_burtle_matches_node_ld);
  RUN_TEST(test_frame_checksum);
  RUN_TEST(test_parse_request_rejects_garbage);
  RUN_TEST(test_wake_response_and_listener);
  RUN_TEST(test_seed_challenge_handshake);
  RUN_TEST(test_model_request);
  RUN_TEST(test_place_and_remove_events);
  RUN_TEST(test_place_limits);
  RUN_TEST(test_index_of);
  RUN_TEST(test_wake_reannounces_tokens);
  RUN_TEST(test_vehicle_read_and_write);
  RUN_TEST(test_light_commands);
  RUN_TEST(test_display_color_mapping);
  RUN_TEST(test_uid_helpers);
  return UNITY_END();
}
