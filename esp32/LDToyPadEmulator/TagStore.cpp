/*
	Copyright © 2023 Berny23, Cort1237 and many more

	This file is part of "Toy Pad Emulator for Lego Dimensions" which is released under the "MIT" license.
	See file "LICENSE" or go to "https://choosealicense.com/licenses/mit" for full license details.
*/


#include "TagStore.h"

#include "Catalog.h"

#include <stdio.h>
#include <string.h>

namespace ld {

static const uint8_t MAGIC[4] = {'L', 'D', 'T', 'T'};
static const uint8_t VERSION = 2;

void TagStore::writeHeader(uint8_t header[HEADER_SIZE]) {
  memcpy(header, MAGIC, sizeof(MAGIC));
  header[sizeof(MAGIC)] = VERSION;
}

bool TagStore::validHeader(const uint8_t header[HEADER_SIZE]) {
  return memcmp(header, MAGIC, sizeof(MAGIC)) == 0 && header[sizeof(MAGIC)] == VERSION;
}

bool TagStore::loadRecord(const uint8_t r[RECORD_SIZE]) {
  if (count_ >= MAX_TAGS) return false;
  TagEntry &e = entries_[count_++];
  memcpy(e.uid, r, UID_SIZE);
  e.id = readU16LE(r + UID_SIZE);
  e.upgradesP23 = readU32LE(r + UID_SIZE + 2);
  e.upgradesP25 = readU32LE(r + UID_SIZE + 6);
  return true;
}

size_t TagStore::encode(size_t first, uint8_t *out, size_t maxRecords) const {
  size_t n = 0;
  for (size_t i = first; i < count_ && n < maxRecords; i++, n++) {
    const TagEntry &e = entries_[i];
    uint8_t *r = out + n * RECORD_SIZE;
    memcpy(r, e.uid, UID_SIZE);
    writeU16LE(r + UID_SIZE, e.id);
    writeU32LE(r + UID_SIZE + 2, e.upgradesP23);
    writeU32LE(r + UID_SIZE + 6, e.upgradesP25);
  }
  return n;
}

TagEntry *TagStore::find(const uint8_t uid[UID_SIZE]) {
  for (size_t i = 0; i < count_; i++)
    if (memcmp(entries_[i].uid, uid, UID_SIZE) == 0) return &entries_[i];
  return nullptr;
}

TagEntry *TagStore::add() {
  if (count_ >= MAX_TAGS) return nullptr;
  TagEntry *e = &entries_[count_++];
  memset(e, 0, sizeof(*e));
  dirty_ = true;
  return e;
}

bool TagStore::erase(const uint8_t uid[UID_SIZE]) {
  TagEntry *e = find(uid);
  if (!e) return false;
  size_t i = e - entries_;
  memmove(&entries_[i], &entries_[i + 1], (count_ - i - 1) * sizeof(TagEntry));
  count_--;
  dirty_ = true;
  return true;
}

void TagStore::render(TagJsonCursor &c, const ToyPadEmulator &pad) const {
  c.pendingPos = 0;
  int len;
  if (c.next < count_) {
    const TagEntry &e = entries_[c.next];
    char uid[UID_STRING_SIZE];
    formatUid(e.uid, uid);
    len = snprintf(c.pending, sizeof(c.pending), "%c{\"uid\":\"%s\",\"id\":%u,\"type\":\"%s\",\"index\":%d,\"name\":\"%s\"}",
                   c.next == 0 ? '[' : ',', uid, e.id, isVehicleId(e.id) ? "vehicle" : "character", pad.indexOf(e.uid),
                   catalogName(e.id));
    c.next++;
  } else {
    len = snprintf(c.pending, sizeof(c.pending), "%s", c.next == 0 ? "[]" : "]");
    c.done = true;
  }
  if (len < 0) len = 0;
  c.pendingLen = (size_t)len < sizeof(c.pending) ? (size_t)len : sizeof(c.pending) - 1;
}

size_t TagStore::writeJson(TagJsonCursor &c, const ToyPadEmulator &pad, uint8_t *buffer, size_t maxLen) const {
  size_t written = 0;
  while (written < maxLen) {
    if (c.pendingPos == c.pendingLen) {
      if (c.done) break;
      render(c, pad);
    }
    size_t n = c.pendingLen - c.pendingPos;
    if (n > maxLen - written) n = maxLen - written;
    memcpy(buffer + written, c.pending + c.pendingPos, n);
    c.pendingPos += n;
    written += n;
  }
  return written;
}

} // namespace ld
