#pragma once

/// @file zxkeys.h
/// @brief ZX key names for the key, chord and macro targets (mapping-and-profiles.md §1.2): the names of
/// DebugKeyboardManager::ResolveKeyName, case-insensitive, each giving the plain matrix keys it holds. A ZXKEY_EXT_*
/// composite (EDIT, UP, '"') is its shift and its key, so the core's host key overlay needs matrix positions only.
/// The values are ZXKeysEnum's (the library does not include the keyboard); a core test checks the two agree.

#include <cstdint>
#include <string>
#include <vector>

namespace controllers
{

/// "space" -> {SPACE}, "edit" -> {CAPS, 1}, "sym+p" -> {SYM, P}: `+` joins keys held together (the plus key
/// is "+" alone, "plus" in a join). Appends to @p keys without repeats; false for an unknown name
bool ZxKeysFromName(const std::string& name, std::vector<uint8_t>& keys);
/// Every single name (not the `+` joins)
std::vector<std::string> KnownZxKeyNames();

}  // namespace controllers
