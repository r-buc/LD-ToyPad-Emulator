/*
	Copyright © 2023 Berny23, Cort1237 and many more

	This file is part of "Toy Pad Emulator for Lego Dimensions" which is released under the "MIT" license.
	See file "LICENSE" or go to "https://choosealicense.com/licenses/mit" for full license details.
*/


// The user's toy tags in a fixed size array (17 bytes per tag in RAM and on
// flash). Names are not stored, they come from the compiled-in catalog, and
// the type follows from the id (vehicles >= 1000). Positions are not stored
// either, they are taken from the emulated pad. writeJson() renders the
// minified JSON for the web UI in small chunks. Hardware independent.

#pragma once

#include "ToyPadEmulator.h"
#include "ToyPadProtocol.h"

#include <stddef.h>
#include <stdint.h>

namespace ld {

// Bounds RAM (17 bytes per tag) and the size of the toy box in the web UI.
#ifndef TOYPAD_MAX_TAGS
#define TOYPAD_MAX_TAGS 400
#endif
constexpr size_t MAX_TAGS = TOYPAD_MAX_TAGS;

struct __attribute__((packed)) TagEntry {
  uint8_t uid[UID_SIZE];
  uint16_t id;
  uint32_t upgradesP23;
  uint32_t upgradesP25;
};

// Position of a JSON rendering that spans several writeJson() calls.
struct TagJsonCursor {
  size_t next = 0;   // next entry to render
  bool done = false; // closing bracket rendered
  char pending[128]; // rendered text not yet copied out
  size_t pendingLen = 0;
  size_t pendingPos = 0;
};

class TagStore {
public:
  // File format: "LDTT", version byte, then RECORD_SIZE bytes per tag:
  // uid[7], id (LE u16), upgradesP23 (LE u32), upgradesP25 (LE u32).
  static constexpr size_t HEADER_SIZE = 5;
  static constexpr size_t RECORD_SIZE = UID_SIZE + 2 + 4 + 4;

  static void writeHeader(uint8_t header[HEADER_SIZE]);
  static bool validHeader(const uint8_t header[HEADER_SIZE]);
  // Loading: clear(), then loadRecord() for every record of the file.
  // Returns false if the record is invalid or the store is full.
  bool loadRecord(const uint8_t record[RECORD_SIZE]);
  // Encodes up to maxRecords tags starting at `first`; returns the number of records written to `out`.
  size_t encode(size_t first, uint8_t *out, size_t maxRecords) const;

  TagEntry *find(const uint8_t uid[UID_SIZE]);
  // Returns the new entry (all fields zero) or nullptr if the store is full.
  TagEntry *add();
  bool erase(const uint8_t uid[UID_SIZE]);
  void clear() { count_ = 0; }
  size_t size() const { return count_; }

  // Renders the tags as the minified JSON array read by the web UI
  // ([{"uid":"…","id":1,"type":"character","index":-1,"name":"Batman"},…]).
  // Fills up to maxLen bytes; returns 0 when the document is complete.
  size_t writeJson(TagJsonCursor &cursor, const ToyPadEmulator &pad, uint8_t *buffer, size_t maxLen) const;

  // Set whenever the persisted representation changed.
  bool dirty() const { return dirty_; }
  void markDirty() { dirty_ = true; }
  void clearDirty() { dirty_ = false; }

private:
  void render(TagJsonCursor &cursor, const ToyPadEmulator &pad) const;

  TagEntry entries_[MAX_TAGS];
  size_t count_ = 0;
  bool dirty_ = false;
};

} // namespace ld
