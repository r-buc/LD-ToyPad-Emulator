/*
	Copyright © 2023 Berny23, Cort1237 and many more

	This file is part of "Toy Pad Emulator for Lego Dimensions" which is released under the "MIT" license.
	See file "LICENSE" or go to "https://choosealicense.com/licenses/mit" for full license details.
*/


// The user's toy tags in a fixed size array (20 bytes per tag in RAM,
// 18 bytes per record on flash). Names are not stored, they come from the
// compiled-in catalog. writeJson() renders the minified JSON for the web UI
// in small chunks, so the whole document is never held in memory.

#pragma once

#include "ToyPadProtocol.h"

#include <FS.h>

#include <cstddef>
#include <cstdint>

namespace ld {

// Bounds RAM (20 bytes per tag) and the size of the toy box in the web UI.
constexpr size_t MAX_TAGS = 400;

enum TagType : uint8_t { TAG_CHARACTER = 0, TAG_VEHICLE = 1 };

struct TagEntry {
  uint8_t uid[UID_SIZE];
  uint8_t type;  // TagType
  int8_t index;  // position on the pad (1..7) or -1 in the toy box; not persisted
  uint16_t id;
  uint32_t upgradesP23;
  uint32_t upgradesP25;
};

// Position of a JSON rendering that spans several writeJson() calls.
struct TagJsonCursor {
  size_t next = 0;   // next entry to render
  bool done = false; // closing bracket rendered
  char pending[160]; // rendered text not yet copied out
  size_t pendingLen = 0;
  size_t pendingPos = 0;
};

class TagStore {
public:
  // File format: "LDTT", version byte, then RECORD_SIZE bytes per tag:
  // uid[7], type, id (LE u16), upgradesP23 (LE u32), upgradesP25 (LE u32).
  static constexpr size_t HEADER_SIZE = 5;
  static constexpr size_t RECORD_SIZE = UID_SIZE + 1 + 2 + 4 + 4;

  static void writeHeader(uint8_t header[HEADER_SIZE]);
  // Reads the tags from a file. Returns false (and leaves the store empty) if the data is invalid.
  bool load(fs::File &in);
  // Encodes up to maxRecords tags starting at `first`; returns the number of records written to `out`.
  size_t encode(size_t first, uint8_t *out, size_t maxRecords) const;

  TagEntry *find(const uint8_t uid[UID_SIZE]);
  TagEntry *findByIndex(int index);
  // Returns the new entry (all fields zero, index -1) or nullptr if the store is full.
  TagEntry *add();
  bool erase(const uint8_t uid[UID_SIZE]);
  bool setIndex(const uint8_t uid[UID_SIZE], int index);
  // Marks every tag as being in the toy box (index -1).
  void resetIndexes();
  size_t size() const { return count_; }

  // Renders the tags as the minified JSON array read by the web UI
  // ([{"uid":"…","id":1,"type":"character","index":-1,"name":"Batman"},…]).
  // Fills up to maxLen bytes; returns 0 when the document is complete.
  size_t writeJson(TagJsonCursor &cursor, uint8_t *buffer, size_t maxLen) const;

  // Set whenever the persisted representation changed.
  bool dirty() const { return dirty_; }
  void markDirty() { dirty_ = true; }
  void clearDirty() { dirty_ = false; }

private:
  void render(TagJsonCursor &cursor) const;

  TagEntry entries_[MAX_TAGS];
  size_t count_ = 0;
  bool dirty_ = false;
};

} // namespace ld
