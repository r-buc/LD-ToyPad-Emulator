// Host side unit tests for the request body reader (make -C esp32/test).

#include "JsonFields.h"
#include <unity.h>

using namespace ld;

void setUp() {}
void tearDown() {}

void test_read_uint() {
  const char *body = "{\"uid\":\"04A1B2C3D4E580\", \"position\" : 2,\"index\":7}";
  uint32_t v = 0;
  TEST_ASSERT_TRUE(readUInt(body, "position", 1, 3, v));
  TEST_ASSERT_EQUAL(2, v);
  TEST_ASSERT_TRUE(readUInt(body, "index", 1, 7, v));
  TEST_ASSERT_EQUAL(7, v);
  TEST_ASSERT_FALSE(readUInt(body, "index", 1, 6, v)); // out of range
  TEST_ASSERT_FALSE(readUInt(body, "uid", 0, 100, v));  // not a number
  TEST_ASSERT_FALSE(readUInt(body, "missing", 0, 100, v));
  TEST_ASSERT_FALSE(readUInt(body, "pos", 0, 100, v)); // prefix of a key
}

void test_read_uint_rejects_garbage() {
  uint32_t v;
  TEST_ASSERT_FALSE(readUInt("{\"id\":-1}", "id", 0, 100, v));
  TEST_ASSERT_FALSE(readUInt("{\"id\":1.5}", "id", 0, 100, v));
  TEST_ASSERT_FALSE(readUInt("{\"id\":12abc}", "id", 0, 100, v));
  TEST_ASSERT_FALSE(readUInt("{\"id\":99999999999999999999}", "id", 0, 0xFFFFFFFF, v));
  TEST_ASSERT_FALSE(readUInt("{\"id\"}", "id", 0, 100, v));
  TEST_ASSERT_FALSE(readUInt("", "id", 0, 100, v));
  TEST_ASSERT_TRUE(readUInt("{\"id\":\n42\n}", "id", 0, 100, v));
  TEST_ASSERT_EQUAL(42, v);
}

void test_read_string() {
  char out[15];
  TEST_ASSERT_TRUE(readString("{\"uid\": \"04A1B2C3D4E580\"}", "uid", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("04A1B2C3D4E580", out);
  TEST_ASSERT_TRUE(readString("{\"uid\":\"\"}", "uid", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("", out);
  TEST_ASSERT_FALSE(readString("{\"uid\":\"04A1B2C3D4E5801\"}", "uid", out, sizeof(out))); // too long
  TEST_ASSERT_FALSE(readString("{\"uid\":\"04A1\\\"\"}", "uid", out, sizeof(out)));       // escape sequence
  TEST_ASSERT_FALSE(readString("{\"uid\":\"04A1", "uid", out, sizeof(out)));              // unterminated
  TEST_ASSERT_FALSE(readString("{\"uid\":12}", "uid", out, sizeof(out)));                 // not a string
  TEST_ASSERT_FALSE(readString("{\"id\":12}", "uid", out, sizeof(out)));                  // missing
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_read_uint);
  RUN_TEST(test_read_uint_rejects_garbage);
  RUN_TEST(test_read_string);
  return UNITY_END();
}
