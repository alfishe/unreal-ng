// The scenario a deck plays when the daemon was given none: the UnrealNG look, made for the deck's model from the
// shipped theme ($data/streamdeck/themes/unrealng, sdb/theme.h). Pictures only: its keys do nothing.
//
//   Plus XL (the theme's own deck)  the theme's icons at their keys (rows 0-2; row 3 blank), the banner on the strip
//   Plus, Neo (a strip / window)    the banner on the strip, icons on the keys in the theme's order
//   decks without a strip           the theme's logo (the banner's middle) across the middle row, icons on the
//                                   other keys
//   no key screens (Pedal)          nothing to show
//   no data directory               a drawn "UnrealNG" title (the strip, or the middle key)
//
// Never the time-travel timeline: that is a strip readout, never keys, and idle has no emulator.
// Paths in the document are $data paths (scenarios.md §3), so it reads as a shipped scenario would.
#pragma once
#include <filesystem>
#include <nlohmann/json.hpp>

#include "sdb/models.h"

namespace sdb::idle
{

inline constexpr const char* kThemeDir = "streamdeck/themes/unrealng";  // under the data directory
inline constexpr const char* kTitle = "UnrealNG";

// `dataRoot`: the data directory (sdb/datadir.h); empty = not found (the drawn title).
[[nodiscard]] nlohmann::json Document(const Model& m, const std::filesystem::path& dataRoot);

}  // namespace sdb::idle
