/*
	Copyright © 2023 Berny23, Cort1237 and many more

	This file is part of "Toy Pad Emulator for Lego Dimensions" which is released under the "MIT" license.
	See file "LICENSE" or go to "https://choosealicense.com/licenses/mit" for full license details.
*/

#include "TagStore.h"

#include <algorithm>
#include <cstdlib>

namespace ld {

namespace {
int readIndex(JsonVariantConst v) {
  // The Node.js version stores "-1" as a string and placed positions as numbers.
  if (v.is<int>()) return v.as<int>();
  if (v.is<const char *>()) return atoi(v.as<const char *>());
  return -1;
}
} // namespace

bool TagStore::loadFrom(const JsonDocument &doc) {
  entries_.clear();
  JsonArrayConst array = doc.as<JsonArrayConst>();
  if (array.isNull()) return false;
  for (JsonObjectConst obj : array) {
    TagEntry e;
    e.name = obj["name"] | "N/A";
    e.id = obj["id"] | 0;
    e.uid = obj["uid"] | "";
    e.index = readIndex(obj["index"]);
    e.type = obj["type"] | "character";
    e.vehicleUpgradesP23 = obj["vehicleUpgradesP23"] | 0u;
    e.vehicleUpgradesP25 = obj["vehicleUpgradesP25"] | 0u;
    if (!e.uid.empty()) entries_.push_back(e);
  }
  return true;
}

void TagStore::saveTo(JsonDocument &doc) const {
  doc.clear();
  JsonArray array = doc.to<JsonArray>();
  for (const auto &e : entries_) {
    JsonObject obj = array.add<JsonObject>();
    obj["name"] = e.name;
    obj["id"] = e.id;
    obj["uid"] = e.uid;
    obj["index"] = e.index;
    obj["type"] = e.type;
    obj["vehicleUpgradesP23"] = e.vehicleUpgradesP23;
    obj["vehicleUpgradesP25"] = e.vehicleUpgradesP25;
  }
}

TagEntry *TagStore::find(const std::string &uid) {
  for (auto &e : entries_)
    if (e.uid == uid) return &e;
  return nullptr;
}

const TagEntry *TagStore::find(const std::string &uid) const {
  for (const auto &e : entries_)
    if (e.uid == uid) return &e;
  return nullptr;
}

TagEntry *TagStore::findByIndex(int index) {
  for (auto &e : entries_)
    if (e.index == index) return &e;
  return nullptr;
}

TagEntry &TagStore::add(const TagEntry &entry) {
  entries_.push_back(entry);
  dirty_ = true;
  return entries_.back();
}

bool TagStore::erase(const std::string &uid) {
  auto it = std::find_if(entries_.begin(), entries_.end(), [&](const TagEntry &e) { return e.uid == uid; });
  if (it == entries_.end()) return false;
  entries_.erase(it);
  dirty_ = true;
  return true;
}

bool TagStore::setIndex(const std::string &uid, int index) {
  TagEntry *e = find(uid);
  if (!e) return false;
  if (e->index != index) {
    e->index = index;
    dirty_ = true;
  }
  return true;
}

void TagStore::resetIndexes() {
  for (auto &e : entries_) {
    if (e.index != -1) {
      e.index = -1;
      dirty_ = true;
    }
  }
}

bool Catalog::loadFrom(const JsonDocument &doc) {
  entries_.clear();
  JsonArrayConst array = doc.as<JsonArrayConst>();
  if (array.isNull()) return false;
  for (JsonObjectConst obj : array) {
    Entry e;
    e.id = obj["id"] | 0;
    e.name = obj["name"] | "N/A";
    entries_.push_back(e);
  }
  std::sort(entries_.begin(), entries_.end(), [](const Entry &a, const Entry &b) { return a.id < b.id; });
  return true;
}

const Catalog::Entry *Catalog::lookup(uint16_t id) const {
  auto it = std::lower_bound(entries_.begin(), entries_.end(), id, [](const Entry &e, uint16_t v) { return e.id < v; });
  return it != entries_.end() && it->id == id ? &*it : nullptr;
}

std::string Catalog::name(uint16_t id) const {
  const Entry *e = lookup(id);
  return e ? e->name : "N/A";
}

bool Catalog::contains(uint16_t id) const { return lookup(id) != nullptr; }

} // namespace ld
