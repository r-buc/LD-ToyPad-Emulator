/*
	Copyright © 2023 Berny23, Cort1237 and many more

	This file is part of "Toy Pad Emulator for Lego Dimensions" which is released under the "MIT" license.
	See file "LICENSE" or go to "https://choosealicense.com/licenses/mit" for full license details.
*/


#include "TagStore.h"

#include "Catalog.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ld {

namespace {
bool sameUid(const TagEntry &e, const uint8_t uid[UID_SIZE]) { return memcmp(e.uid, uid, UID_SIZE) == 0; }
} // namespace

bool TagStore::validHeader(const uint8_t *header) {
  return memcmp(header, MAGIC, sizeof(MAGIC)) == 0 && header[sizeof(MAGIC)] == VERSION;
}

void TagStore::encode(const TagEntry &e, uint8_t *r) {
  memcpy(r, e.uid, UID_SIZE);
  r[UID_SIZE] = static_cast<uint8_t>(e.type);
  writeU16LE(r + UID_SIZE + 1, e.id);
  writeU32LE(r + UID_SIZE + 3, e.upgradesP23);
  writeU32LE(r + UID_SIZE + 7, e.upgradesP25);
}

bool TagStore::decode(const uint8_t *r, TagEntry &e) {
  if (r[UID_SIZE] > static_cast<uint8_t>(TagType::Vehicle)) return false;
  memcpy(e.uid, r, UID_SIZE);
  e.type = static_cast<TagType>(r[UID_SIZE]);
  e.index = -1;
  e.id = readU16LE(r + UID_SIZE + 1);
  e.upgradesP23 = readU32LE(r + UID_SIZE + 3);
  e.upgradesP25 = readU32LE(r + UID_SIZE + 7);
  return true;
}

TagEntry *TagStore::find(const uint8_t uid[UID_SIZE]) {
  for (auto &e : entries_)
    if (sameUid(e, uid)) return &e;
  return nullptr;
}

TagEntry *TagStore::findByIndex(int index) {
  for (auto &e : entries_)
    if (e.index == index) return &e;
  return nullptr;
}

void TagStore::add(const TagEntry &entry) {
  entries_.push_back(entry);
  dirty_ = true;
}

bool TagStore::erase(const uint8_t uid[UID_SIZE]) {
  auto it = std::find_if(entries_.begin(), entries_.end(), [&](const TagEntry &e) { return sameUid(e, uid); });
  if (it == entries_.end()) return false;
  entries_.erase(it);
  dirty_ = true;
  return true;
}

bool TagStore::setIndex(const uint8_t uid[UID_SIZE], int index) {
  TagEntry *e = find(uid);
  if (!e) return false;
  e->index = static_cast<int8_t>(index); // positions are runtime state, nothing to persist
  return true;
}

void TagStore::resetIndexes() {
  for (auto &e : entries_) e.index = -1;
}

void TagJsonWriter::renderNext() {
  pendingPos_ = 0;
  int len;
  if (next_ < entries_.size()) {
    const TagEntry &e = entries_[next_];
    len = snprintf(pending_, sizeof(pending_), "%c{\"uid\":\"%s\",\"id\":%u,\"type\":\"%s\",\"index\":%d,\"name\":\"%s\"}",
                   next_ == 0 ? '[' : ',', formatUid(e.uid).c_str(), e.id,
                   e.type == TagType::Character ? "character" : "vehicle", e.index, catalogName(e.id));
    next_++;
  } else {
    len = snprintf(pending_, sizeof(pending_), "%s", entries_.empty() ? "[]" : "]");
    done_ = true;
  }
  pendingLen_ = len < 0 ? 0 : std::min(static_cast<size_t>(len), sizeof(pending_) - 1);
}

size_t TagJsonWriter::write(uint8_t *buffer, size_t maxLen) {
  size_t written = 0;
  while (written < maxLen) {
    if (pendingPos_ == pendingLen_) {
      if (done_) break;
      renderNext();
    }
    size_t n = std::min(maxLen - written, pendingLen_ - pendingPos_);
    memcpy(buffer + written, pending_ + pendingPos_, n);
    pendingPos_ += n;
    written += n;
  }
  return written;
}

} // namespace ld
