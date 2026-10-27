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

/// Memories that are not the machine's RAM (2026-10-08): the write journal, the access probe and the coverage index
/// name a byte of such a memory by a virtual page of its space - above every RAM page - and its offset in that page
/// (the low 14 bits of the record's address). The Sprinter's video RAM (256 KB: 16 pages) and its fast RAM, the
/// "cache" (64 KB: 4 pages). Worked example: video RAM offset #12345 is page kVramPageBase + 4, address #2345.
enum class TTDMemorySpace : uint8_t
{
    Ram = 0,     ///< machine RAM pages 0..255 (the address is the Z80 address)
    Vram = 1,    ///< a video RAM of its own (the Sprinter's)
    Cache = 2,   ///< the CPU's fast RAM / cache pages (the Sprinter's)
};

constexpr PhysPage kVramPageBase = 0x110;
constexpr PhysPage kCachePageBase = 0x120;
constexpr uint32_t kSpacePages = 16;          ///< pages per space (16 x 16 KB)
constexpr uint32_t kSpacePageBytes = 0x4000;

/// The space a page belongs to (RAM for 0..255 and kPhysPageNone)
inline TTDMemorySpace SpaceOfPage(PhysPage page)
{
    if (page >= kVramPageBase && page < kVramPageBase + kSpacePages)
        return TTDMemorySpace::Vram;
    if (page >= kCachePageBase && page < kCachePageBase + kSpacePages)
        return TTDMemorySpace::Cache;
    return TTDMemorySpace::Ram;
}

/// The virtual page of `offset` in `space` (kPhysPageNone for RAM or an offset past the space)
inline PhysPage SpacePage(TTDMemorySpace space, uint32_t offset)
{
    if (space == TTDMemorySpace::Ram || offset >= kSpacePages * kSpacePageBytes)
        return kPhysPageNone;
    return static_cast<PhysPage>((space == TTDMemorySpace::Vram ? kVramPageBase : kCachePageBase) + offset / kSpacePageBytes);
}

/// The offset in its space of the byte a virtual page and an address name
inline uint32_t SpaceOffset(PhysPage page, uint16_t addr)
{
    const PhysPage base = SpaceOfPage(page) == TTDMemorySpace::Vram ? kVramPageBase : kCachePageBase;
    return static_cast<uint32_t>(page - base) * kSpacePageBytes + (addr & (kSpacePageBytes - 1));
}

inline const char* SpaceName(TTDMemorySpace space)
{
    return space == TTDMemorySpace::Vram ? "vram" : space == TTDMemorySpace::Cache ? "cache" : "ram";
}

}  // namespace ttd
