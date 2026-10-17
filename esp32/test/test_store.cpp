// Host side unit tests for the tag store and the compiled-in catalog (make -C esp32/test).

#include "Catalog.h"
#include "TagStore.h"
#include "ToyPadEmulator.h"
#include <unity.h>

#include <cstring>
#include <string>

using namespace ld;

namespace {

const uint8_t UID_A[UID_SIZE] = {0x04, 0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0x80};
const uint8_t UID_B[UID_SIZE] = {0x04, 0x01, 0x02, 0x03, 0x04, 0x05, 0x80};

// Large object: keep it out of the stack.
TagStore store;
TagStore loaded;
ToyPadEmulator pad;

TagEntry *addTag(const uint8_t uid[UID_SIZE], uint16_t id, uint32_t p23 = 0, uint32_t p25 = 0) {
  TagEntry *e = store.add();
  TEST_ASSERT_NOT_NULL(e);
  memcpy(e->uid, uid, UID_SIZE);
  e->id = id;
  e->upgradesP23 = p23;
  e->upgradesP25 = p25;
  return e;
}

std::string json(const TagStore &s, size_t chunk) {
  TagJsonCursor cursor;
  std::string out;
  uint8_t buffer[256];
  for (size_t n; (n = s.writeJson(cursor, pad, buffer, chunk)) > 0;) {
    TEST_ASSERT_TRUE(n <= chunk);
    out.append(reinterpret_cast<char *>(buffer), n);
  }
  return out;
}

// Encodes `s` like ToyPadService::persist() does.
size_t save(const TagStore &s, uint8_t *file) {
  TagStore::writeHeader(file);
  size_t len = TagStore::HEADER_SIZE;
  for (size_t n, first = 0; (n = s.encode(first, file + len, 3)) > 0; first += n) len += n * TagStore::RECORD_SIZE;
  return len;
}

// Decodes like ToyPadService::begin() does.
bool load(TagStore &s, const uint8_t *file, size_t len) {
  s.clear();
  if (len < TagStore::HEADER_SIZE || !TagStore::validHeader(file)) return false;
  for (size_t pos = TagStore::HEADER_SIZE; pos < len; pos += TagStore::RECORD_SIZE)
    if (len - pos < TagStore::RECORD_SIZE || !s.loadRecord(file + pos)) return false;
  return true;
}

} // namespace

void setUp() {
  store.clear();
  store.clearDirty();
  loaded.clear();
  pad.clear();
}
void tearDown() {}

void test_entry_layout() {
  TEST_ASSERT_EQUAL(17, sizeof(TagEntry));
  TEST_ASSERT_EQUAL(17, TagStore::RECORD_SIZE);
}

void test_add_find_erase() {
  TEST_ASSERT_EQUAL(0, store.size());
  TEST_ASSERT_NULL(store.find(UID_A));
  addTag(UID_A, 1);
  addTag(UID_B, 1000);
  TEST_ASSERT_TRUE(store.dirty());
  TEST_ASSERT_EQUAL(2, store.size());
  TEST_ASSERT_EQUAL(1000, store.find(UID_B)->id);

  store.clearDirty();
  TEST_ASSERT_TRUE(store.erase(UID_A));
  TEST_ASSERT_TRUE(store.dirty());
  TEST_ASSERT_FALSE(store.erase(UID_A));
  TEST_ASSERT_NULL(store.find(UID_A));
  TEST_ASSERT_EQUAL(1000, store.find(UID_B)->id);
  TEST_ASSERT_EQUAL(1, store.size());
}

void test_full_store() {
  uint8_t uid[UID_SIZE] = {0x04, 0, 0, 0, 0, 0, 0x80};
  for (size_t i = 0; i < MAX_TAGS; i++) {
    uid[1] = static_cast<uint8_t>(i);
    uid[2] = static_cast<uint8_t>(i >> 8);
    addTag(uid, 1);
  }
  TEST_ASSERT_NULL(store.add());
  TEST_ASSERT_EQUAL(MAX_TAGS, store.size());

  static uint8_t file[TagStore::HEADER_SIZE + (MAX_TAGS + 1) * TagStore::RECORD_SIZE];
  size_t len = save(store, file);
  TEST_ASSERT_TRUE(load(loaded, file, len));
  TEST_ASSERT_EQUAL(MAX_TAGS, loaded.size());
  // One record too many
  memcpy(file + len, file + TagStore::HEADER_SIZE, TagStore::RECORD_SIZE);
  TEST_ASSERT_FALSE(load(loaded, file, len + TagStore::RECORD_SIZE));
}

void test_binary_round_trip() {
  addTag(UID_A, 1);
  addTag(UID_B, 1050, 0xefffffff, 0x12345678);
  uint8_t file[TagStore::HEADER_SIZE + 2 * TagStore::RECORD_SIZE];
  TEST_ASSERT_EQUAL(sizeof(file), save(store, file));
  TEST_ASSERT_EQUAL_MEMORY("LDTT\x02", file, TagStore::HEADER_SIZE);
  // uid, id (LE16), upgrades (LE32)
  const uint8_t second[] = {0x04, 0x01, 0x02, 0x03, 0x04, 0x05, 0x80, 0x1a, 0x04,
                            0xff, 0xff, 0xff, 0xef, 0x78, 0x56, 0x34, 0x12};
  TEST_ASSERT_EQUAL_MEMORY(second, file + TagStore::HEADER_SIZE + TagStore::RECORD_SIZE, sizeof(second));

  TEST_ASSERT_TRUE(load(loaded, file, sizeof(file)));
  TEST_ASSERT_EQUAL(2, loaded.size());
  TEST_ASSERT_FALSE(loaded.dirty());
  const TagEntry *b = loaded.find(UID_B);
  TEST_ASSERT_NOT_NULL(b);
  TEST_ASSERT_EQUAL(1050, b->id);
  TEST_ASSERT_EQUAL_HEX32(0xefffffff, b->upgradesP23);
  TEST_ASSERT_EQUAL_HEX32(0x12345678, b->upgradesP25);
  TEST_ASSERT_EQUAL(1, loaded.find(UID_A)->id);
}

void test_invalid_files() {
  addTag(UID_A, 1);
  uint8_t file[TagStore::HEADER_SIZE + TagStore::RECORD_SIZE];
  size_t len = save(store, file);
  TEST_ASSERT_TRUE(load(loaded, file, len));
  TEST_ASSERT_FALSE(load(loaded, file, len - 1)); // truncated record
  TEST_ASSERT_FALSE(load(loaded, file, 3));      // truncated header
  file[4] = 1;                                   // old version
  TEST_ASSERT_FALSE(load(loaded, file, len));
  file[4] = 2;
  file[0] = '{'; // e.g. a toytags.json
  TEST_ASSERT_FALSE(load(loaded, file, len));
}

void test_json_empty() {
  TEST_ASSERT_EQUAL_STRING("[]", json(store, 64).c_str());
  TEST_ASSERT_EQUAL_STRING("[]", json(store, 1).c_str());
}

void test_json_rendering() {
  addTag(UID_A, 1);
  addTag(UID_B, 1000, 0xefffffff, 0xefffffff);
  addTag(UID_A, 999); // unknown id (duplicate UIDs are never created, only used to test the name)
  TEST_ASSERT_TRUE(pad.place(makeVehicleToken(1000, UID_B, 0, 0), 3, 5));

  const char *expected =
    "[{\"uid\":\"04A1B2C3D4E580\",\"id\":1,\"type\":\"character\",\"index\":-1,\"name\":\"Batman\"},"
    "{\"uid\":\"04010203040580\",\"id\":1000,\"type\":\"vehicle\",\"index\":5,\"name\":\"Police Car\"},"
    "{\"uid\":\"04A1B2C3D4E580\",\"id\":999,\"type\":\"character\",\"index\":-1,\"name\":\"N/A\"}]";
  for (size_t chunk = 1; chunk <= 200; chunk++) TEST_ASSERT_EQUAL_STRING(expected, json(store, chunk).c_str());
}

void test_catalog() {
  TEST_ASSERT_EQUAL_STRING("Batman", findCharacter(1)->name);
  TEST_ASSERT_EQUAL_STRING("Police Car", findVehicle(1000)->name);
  TEST_ASSERT_NULL(findCharacter(1000));
  TEST_ASSERT_NULL(findVehicle(1));
  TEST_ASSERT_NULL(findCharacter(0));
  TEST_ASSERT_EQUAL_STRING("Batman", catalogName(1));
  TEST_ASSERT_EQUAL_STRING("Police Car", catalogName(1000));
  TEST_ASSERT_EQUAL_STRING("N/A", catalogName(65535));
  TEST_ASSERT_FALSE(isVehicleId(999));
  TEST_ASSERT_TRUE(isVehicleId(1000));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_entry_layout);
  RUN_TEST(test_add_find_erase);
  RUN_TEST(test_full_store);
  RUN_TEST(test_binary_round_trip);
  RUN_TEST(test_invalid_files);
  RUN_TEST(test_json_empty);
  RUN_TEST(test_json_rendering);
  RUN_TEST(test_catalog);
  return UNITY_END();
}
