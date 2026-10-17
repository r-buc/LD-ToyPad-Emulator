/*
	Copyright © 2023 Berny23, Cort1237 and many more

	This file is part of "Toy Pad Emulator for Lego Dimensions" which is released under the "MIT" license.
	See file "LICENSE" or go to "https://choosealicense.com/licenses/mit" for full license details.
*/


#include "Catalog.h"

#include "CatalogData.h"

#include <algorithm>

namespace ld {

namespace {
template <size_t N> const CatalogEntry *find(const CatalogEntry (&table)[N], uint16_t id) {
  const CatalogEntry *end = table + N;
  const CatalogEntry *it = std::lower_bound(table, end, id, [](const CatalogEntry &e, uint16_t v) { return e.id < v; });
  return it != end && it->id == id ? it : nullptr;
}
} // namespace

const CatalogEntry *findCharacter(uint16_t id) { return find(CHARACTERS, id); }

const CatalogEntry *findVehicle(uint16_t id) { return find(VEHICLES, id); }

const char *catalogName(uint16_t id) {
  const CatalogEntry *e = isVehicleId(id) ? findVehicle(id) : findCharacter(id);
  return e ? e->name : "N/A";
}

} // namespace ld
