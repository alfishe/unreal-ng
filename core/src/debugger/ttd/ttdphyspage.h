#pragma once

/// @file ttdphyspage.h
/// @brief The physical RAM page type shared by Memory and time travel.
///
/// A physical page is a 16 KB RAM page index, 0..MAX_RAM_PAGES-1 (0..255 on a
/// 4 MB machine). "No page" - ROM, cache, I/O - needs a value outside that
/// range. It used to be 0xFF in a uint8_t, which is also the last RAM page of
/// a 4 MB machine: writes to page 255 then looked like ROM writes and time
/// travel never recorded them. The type is 16 bits wide so the sentinel can
/// sit outside every real page number.

#include <cstdint>

namespace ttd
{

/// Physical RAM page index, or kPhysPageNone
using PhysPage = uint16_t;

/// "This access has no physical RAM page" (ROM, cache, I/O)
constexpr PhysPage kPhysPageNone = 0xFFFF;

/// Highest page number a caller may name (256-page RAM ceiling)
constexpr PhysPage kPhysPageMax = 255;

}  // namespace ttd
