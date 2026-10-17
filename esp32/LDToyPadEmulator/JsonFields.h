/*
	Copyright © 2023 Berny23, Cort1237 and many more

	This file is part of "Toy Pad Emulator for Lego Dimensions" which is released under the "MIT" license.
	See file "LICENSE" or go to "https://choosealicense.com/licenses/mit" for full license details.
*/

// The web UI sends flat JSON objects with numbers and short strings, e.g.
// {"uid":"04A1B2C3D4E580","position":1,"index":2}. Instead of a JSON library
// the values are looked up directly in the body. Hardware independent.

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ld {

// Reads an unsigned integer field in [min, max]; returns false if missing or out of range.
bool readUInt(const char *body, const char *key, uint32_t min, uint32_t max, uint32_t &out);

// Copies a string field without escape sequences; returns false if missing, not a string or too long.
bool readString(const char *body, const char *key, char *out, size_t outLen);

} // namespace ld
