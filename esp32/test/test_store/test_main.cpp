// Host side unit tests for the toy tag persistence format.

#include <TagStore.h>
#include <unity.h>

#include <string>

using namespace ld;

void setUp() {}
void tearDown() {}

void test_load_node_toytags_format() {
  // Format written by the Node.js version (index "-1" stored as string).
  std::string json = R"([
    {"name": "Batman", "id": 1, "uid": "04A1B2C3D4E580", "index": "-1", "type": "character",
     "vehicleUpgradesP23": 0, "vehicleUpgradesP25": 0},
    {"name": "Police Car", "id": 1000, "uid": "04010203040580", "index": 4, "type": "vehicle",
     "vehicleUpgradesP23": 4026531839, "vehicleUpgradesP25": 4026531839}
  ])";
  TagStore store;
  TEST_ASSERT_TRUE(store.load(json));
  TEST_ASSERT_EQUAL(2, store.size());
  const TagEntry *batman = store.find("04A1B2C3D4E580");
  TEST_ASSERT_NOT_NULL(batman);
  TEST_ASSERT_EQUAL(-1, batman->index);
  TEST_ASSERT_TRUE(batman->isCharacter());
  const TagEntry *car = store.find("04010203040580");
  TEST_ASSERT_NOT_NULL(car);
  TEST_ASSERT_EQUAL(4, car->index);
  TEST_ASSERT_FALSE(car->isCharacter());
  TEST_ASSERT_EQUAL_HEX32(0xefffffff, car->vehicleUpgradesP23);
  TEST_ASSERT_FALSE(store.dirty());
}

void test_round_trip_and_mutations() {
  TagStore store;
  std::string empty = "[]";
  TEST_ASSERT_TRUE(store.load(empty));
  TagEntry e;
  e.name = "Gandalf";
  e.id = 2;
  e.uid = "04AABBCCDDEE80";
  e.type = "character";
  store.add(e);
  TEST_ASSERT_TRUE(store.dirty());
  store.clearDirty();

  TEST_ASSERT_TRUE(store.setIndex("04AABBCCDDEE80", 2));
  TEST_ASSERT_TRUE(store.dirty());
  TEST_ASSERT_EQUAL_PTR(store.find("04AABBCCDDEE80"), store.findByIndex(2));
  TEST_ASSERT_FALSE(store.setIndex("04000000000080", 2));

  std::string out;
  store.save(out);
  TagStore copy;
  TEST_ASSERT_TRUE(copy.load(out));
  TEST_ASSERT_EQUAL(1, copy.size());
  TEST_ASSERT_EQUAL_STRING("Gandalf", copy.entries()[0].name.c_str());
  TEST_ASSERT_EQUAL(2, copy.entries()[0].index);

  copy.resetIndexes();
  TEST_ASSERT_EQUAL(-1, copy.entries()[0].index);
  TEST_ASSERT_TRUE(copy.erase("04AABBCCDDEE80"));
  TEST_ASSERT_FALSE(copy.erase("04AABBCCDDEE80"));
  TEST_ASSERT_EQUAL(0, copy.size());
}

void test_invalid_json() {
  TagStore store;
  std::string bad = "{not json";
  TEST_ASSERT_FALSE(store.load(bad));
  TEST_ASSERT_EQUAL(0, store.size());
  std::string object = R"({"a": 1})";
  TEST_ASSERT_FALSE(store.load(object));
}

void test_catalog_lookup() {
  std::string json = R"([
    {"id": 2, "name": "Gandalf", "world": "The Lord of the Rings", "abilities": "Magic"},
    {"id": 1, "name": "Batman", "world": "DC Comics", "abilities": "Grapple"}
  ])";
  Catalog catalog;
  TEST_ASSERT_TRUE(catalog.load(json));
  TEST_ASSERT_EQUAL(2, catalog.size());
  TEST_ASSERT_EQUAL_STRING("Batman", catalog.name(1).c_str());
  TEST_ASSERT_EQUAL_STRING("Gandalf", catalog.name(2).c_str());
  TEST_ASSERT_EQUAL_STRING("N/A", catalog.name(3).c_str());
  TEST_ASSERT_TRUE(catalog.contains(1));
  TEST_ASSERT_FALSE(catalog.contains(3));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_load_node_toytags_format);
  RUN_TEST(test_round_trip_and_mutations);
  RUN_TEST(test_invalid_json);
  RUN_TEST(test_catalog_lookup);
  return UNITY_END();
}
