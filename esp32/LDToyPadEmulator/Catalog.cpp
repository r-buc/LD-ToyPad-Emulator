/*
	Copyright © 2023 Berny23, Cort1237 and many more

	This file is part of "Toy Pad Emulator for Lego Dimensions" which is released under the "MIT" license.
	See file "LICENSE" or go to "https://choosealicense.com/licenses/mit" for full license details.
*/


#include "Catalog.h"

#include "CatalogData.h"

namespace ld {

namespace {
// Binary search, the generated tables are sorted by id.
const CatalogEntry *find(const CatalogEntry *table, size_t count, uint16_t id) {
  size_t lo = 0, hi = count;
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (table[mid].id == id) return &table[mid];
    if (table[mid].id < id)
      lo = mid + 1;
    else
      hi = mid;
  }
  return nullptr;
}
} // namespace

const CatalogEntry *findCharacter(uint16_t id) { return find(CHARACTERS, sizeof(CHARACTERS) / sizeof(CHARACTERS[0]), id); }

const CatalogEntry *findVehicle(uint16_t id) { return find(VEHICLES, sizeof(VEHICLES) / sizeof(VEHICLES[0]), id); }

const char *catalogName(uint16_t id) {
  const CatalogEntry *e = isVehicleId(id) ? findVehicle(id) : findCharacter(id);
  return e ? e->name : "N/A";
}

} // namespace ld
