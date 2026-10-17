/*
	Copyright © 2023 Berny23, Cort1237 and many more

	This file is part of "Toy Pad Emulator for Lego Dimensions" which is released under the "MIT" license.
	See file "LICENSE" or go to "https://choosealicense.com/licenses/mit" for full license details.
*/

// In-memory list of the user's toy tags, persisted as JSON in exactly the
// same format as the Node.js version's server/json/toytags.json so the web UI
// can keep consuming it unchanged.

#pragma once

#include <ArduinoJson.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ld {

struct TagEntry {
  std::string name;
  uint16_t id = 0;
  std::string uid;
  int index = -1; // position on the pad (1..7) or -1 when in the toy box
  std::string type; // "character" or "vehicle"
  uint32_t vehicleUpgradesP23 = 0;
  uint32_t vehicleUpgradesP25 = 0;

  bool isCharacter() const { return type == "character"; }
};

class TagStore {
public:
  // Loads the store from a JSON array. Returns false (and leaves the store
  // empty) when the input cannot be parsed.
  template <typename TInput> bool load(TInput &input) {
    JsonDocument doc;
    if (deserializeJson(doc, input)) {
      entries_.clear();
      return false;
    }
    return loadFrom(doc);
  }

  // Serialises the store as pretty printed JSON array.
  template <typename TOutput> size_t save(TOutput &output) const {
    JsonDocument doc;
    saveTo(doc);
    return serializeJsonPretty(doc, output);
  }

  bool loadFrom(const JsonDocument &doc);
  void saveTo(JsonDocument &doc) const;

  TagEntry *find(const std::string &uid);
  const TagEntry *find(const std::string &uid) const;
  TagEntry *findByIndex(int index);
  TagEntry &add(const TagEntry &entry);
  bool erase(const std::string &uid);
  bool setIndex(const std::string &uid, int index);
  // Marks every tag as being in the toy box (index -1).
  void resetIndexes();

  const std::vector<TagEntry> &entries() const { return entries_; }
  size_t size() const { return entries_.size(); }

  // Set whenever the persisted representation changed.
  bool dirty() const { return dirty_; }
  void markDirty() { dirty_ = true; }
  void clearDirty() { dirty_ = false; }

private:
  std::vector<TagEntry> entries_;
  bool dirty_ = false;
};

// Read-only id -> name lookup built from charactermap.json / tokenmap.json.
class Catalog {
public:
  template <typename TInput> bool load(TInput &input) {
    JsonDocument filter;
    filter[0]["id"] = true;
    filter[0]["name"] = true;
    JsonDocument doc;
    if (deserializeJson(doc, input, DeserializationOption::Filter(filter))) return false;
    return loadFrom(doc);
  }

  bool loadFrom(const JsonDocument &doc);
  // Returns "N/A" for unknown ids (like the Node.js implementation).
  std::string name(uint16_t id) const;
  bool contains(uint16_t id) const;
  size_t size() const { return entries_.size(); }

private:
  struct Entry {
    uint16_t id;
    std::string name;
  };
  const Entry *lookup(uint16_t id) const;

  std::vector<Entry> entries_; // sorted by id
};

} // namespace ld
