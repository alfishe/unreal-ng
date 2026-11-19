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
/// Every single name (not the `+` joins; `pc.delete` .. `pc.pagedown`, `pc.escape` (`pc.esc`), `pc.tab`, `pc.backspace`,
/// `pc.alt`, `pc.ctrl`, `pc.f1` .. `pc.f12` are also
/// accepted: codes 0xE0.. that only a PS/2 keyboard holds)
std::vector<std::string> KnownZxKeyNames();
/// The name of one plain matrix key as ZxKeysFromName takes it: "caps", "sym", "enter", "space", "a".."z", "0".."9",
/// "pc.delete".. ; "#<hex>" for a code with no name
std::string ZxKeyName(uint8_t code);
/// The keys joined as one chord name ("caps+6"), in the order given
std::string ZxChordName(const std::vector<uint8_t>& keys);

}  // namespace controllers
