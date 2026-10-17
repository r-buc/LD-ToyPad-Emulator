/*
	Copyright © 2023 Berny23, Cort1237 and many more

	This file is part of "Toy Pad Emulator for Lego Dimensions" which is released under the "MIT" license.
	See file "LICENSE" or go to "https://choosealicense.com/licenses/mit" for full license details.
*/


// The user's toy tags in a compact binary form (20 bytes per tag in RAM,
// 18 bytes per record on flash). Names are not stored, they come from the
// compiled-in catalog. TagJsonWriter renders the minified JSON for the web UI
// in small chunks, so the whole document is never held in memory.

#pragma once

#include "ToyPadProtocol.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ld {

enum class TagType : uint8_t { Character = 0, Vehicle = 1 };

struct TagEntry {
  uint8_t uid[UID_SIZE] = {};
  TagType type = TagType::Character;
  int8_t index = -1; // position on the pad (1..7) or -1 in the toy box; not persisted
  uint16_t id = 0;
  uint32_t upgradesP23 = 0;
  uint32_t upgradesP25 = 0;
};

class TagStore {
public:
  // File format: "LDTT", version byte, then RECORD_SIZE bytes per tag:
  // uid[7], type, id (LE u16), upgradesP23 (LE u32), upgradesP25 (LE u32).
  static constexpr uint8_t MAGIC[4] = {'L', 'D', 'T', 'T'};
  static constexpr uint8_t VERSION = 1;
  static constexpr size_t RECORD_SIZE = UID_SIZE + 1 + 2 + 4 + 4;

  // Reads the tags from a stream with read(uint8_t *, size_t). Returns false
  // (and leaves the store empty) if the data is invalid.
  template <typename TStream> bool load(TStream &in) {
    entries_.clear();
    uint8_t header[sizeof(MAGIC) + 1];
    if (in.read(header, sizeof(header)) != sizeof(header) || !validHeader(header)) return false;
    uint8_t record[RECORD_SIZE];
    for (;;) {
      size_t n = in.read(record, RECORD_SIZE);
      if (n == 0) return true;
      TagEntry entry;
      if (n != RECORD_SIZE || !decode(record, entry)) {
        entries_.clear();
        return false;
      }
      entries_.push_back(entry);
    }
  }

  // Writes tags to a stream with write(const uint8_t *, size_t).
  template <typename TStream> static bool save(const std::vector<TagEntry> &entries, TStream &out) {
    uint8_t header[sizeof(MAGIC) + 1] = {MAGIC[0], MAGIC[1], MAGIC[2], MAGIC[3], VERSION};
    if (out.write(header, sizeof(header)) != sizeof(header)) return false;
    uint8_t record[RECORD_SIZE];
    for (const TagEntry &e : entries) {
      encode(e, record);
      if (out.write(record, RECORD_SIZE) != RECORD_SIZE) return false;
    }
    return true;
  }

  TagEntry *find(const uint8_t uid[UID_SIZE]);
  TagEntry *findByIndex(int index);
  void add(const TagEntry &entry);
  bool erase(const uint8_t uid[UID_SIZE]);
  bool setIndex(const uint8_t uid[UID_SIZE], int index);
  // Marks every tag as being in the toy box (index -1).
  void resetIndexes();

  const std::vector<TagEntry> &entries() const { return entries_; }
  size_t size() const { return entries_.size(); }

  // Set whenever the persisted representation changed.
  bool dirty() const { return dirty_; }
  void markDirty() { dirty_ = true; }
  void clearDirty() { dirty_ = false; }

private:
  static bool validHeader(const uint8_t *header);
  static void encode(const TagEntry &entry, uint8_t *record);
  static bool decode(const uint8_t *record, TagEntry &entry);

  std::vector<TagEntry> entries_;
  bool dirty_ = false;
};

// Renders tags as the minified JSON array read by the web UI
// ([{"uid":"…","id":1,"type":"character","index":-1,"name":"Batman"},…])
// into buffers of any size.
class TagJsonWriter {
public:
  explicit TagJsonWriter(std::vector<TagEntry> entries) : entries_(std::move(entries)) {}

  // Fills up to maxLen bytes; returns 0 when the document is complete.
  size_t write(uint8_t *buffer, size_t maxLen);

private:
  void renderNext();

  std::vector<TagEntry> entries_;
  size_t next_ = 0;    // next entry to render
  bool done_ = false;  // closing bracket rendered
  char pending_[160];  // rendered text not yet copied out
  size_t pendingLen_ = 0;
  size_t pendingPos_ = 0;
};

} // namespace ld
