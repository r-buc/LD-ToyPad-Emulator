/*
	Copyright © 2023 Berny23, Cort1237 and many more

	This file is part of "Toy Pad Emulator for Lego Dimensions" which is released under the "MIT" license.
	See file "LICENSE" or go to "https://choosealicense.com/licenses/mit" for full license details.
*/

#include "JsonFields.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

namespace ld {

static const char *skipSpace(const char *p) {
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  return p;
}

// Returns a pointer to the value of "key" or nullptr if the key is missing.
static const char *findValue(const char *body, const char *key) {
  size_t keyLen = strlen(key);
  for (const char *p = strchr(body, '"'); p; p = strchr(p + 1, '"')) {
    if (strncmp(p + 1, key, keyLen) != 0 || p[keyLen + 1] != '"') continue;
    const char *v = skipSpace(p + keyLen + 2);
    if (*v == ':') return skipSpace(v + 1);
  }
  return nullptr;
}

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

} // namespace ld
