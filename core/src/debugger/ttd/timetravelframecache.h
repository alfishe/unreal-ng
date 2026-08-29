#pragma once

/// @file timetravelframecache.h
/// @brief Per-frame CPU/memory/port decode ring for fast reverse browsing.
///
/// Reverse debugging (DeZog history, reverse-continue, scrubbing) reads decoded
/// instruction records for a frame many times. Producing each read by a fresh
/// TTD replay-to-target is O(instructions) per read. This cache turns that into
/// "replay the frame once, then O(1) reads" — see
/// docs/inprogress/2026-08-27-dezog-integration/reverse-debugging.md §5.
///
/// Lifetime: the cache is populated ONLY during a TTD replay pass (the emulator
/// must be Detached / not Recording) and is freed when the session leaves the
/// replay/browse scope (return to present, StartRecording, InvalidateSession).
/// It never lives during live forward recording.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ttd
{

/// @brief Kind of a captured per-instruction access.
enum class TTDAccessKind : uint8_t
{
    MemWrite = 0,
    PortWrite = 1,
    // Reserved for future capture: MemRead = 2, PortRead = 3.
};

/// @brief One memory or port access performed by an instruction.
///
/// Stored in a single shared arena per frame (the "cold" / variable part of the
/// hot/cold split); an entry references its accesses by (offset, count). This
/// keeps the hot record small — measured ~26% less memory and ~20% faster
/// sequential (reverse-continue) scans than inlining the arrays, at the same
/// random-lookup speed and only one extra amortized allocation. See the layout
/// benchmark (`BM_TTD_Layout_*`) and reverse-debugging.md §5.
struct TTDFrameCacheAccess
{
    uint16_t      addr;   ///< Z80 address (memory) or port number (I/O)
    uint8_t       value;  ///< Byte written (writes) / read (reads)
    TTDAccessKind kind;
};

/// @brief One executed instruction's decoded record within a frame (hot part).
///
/// Minimal fixed-length record for fast lookup and memory efficiency. CPU
/// fields mirror the DZRP CMD_GET_REGISTERS layout so a history server can
/// serialize an entry with no extra decode. Variable per-instruction accesses
/// live in TTDFrameCache::accesses, referenced by [accessOffset, +accessCount).
struct TTDFrameCacheEntry
{
    uint32_t tInFrame = 0;  ///< t-state at this instruction's M1 (frame-relative)

    // CPU (DZRP register order)
    uint16_t pc = 0, sp = 0;
    uint16_t af = 0, bc = 0, de = 0, hl = 0, ix = 0, iy = 0;
    uint16_t af2 = 0, bc2 = 0, de2 = 0, hl2 = 0;
    uint8_t  i = 0, r = 0, im = 0;

    uint8_t  opcodes[4] = {0, 0, 0, 0};  ///< bytes at PC
    uint16_t spContent = 0;              ///< word at (SP)

    uint8_t  slotCount = 0;
    uint8_t  slots[8] = {0};             ///< bank per slot

    // Reference into the frame's shared access arena (cold part).
    uint32_t accessOffset = 0;
    uint16_t accessCount = 0;
};

/// @brief Decoded records for one recorded frame, in execution order, with a
/// single shared access arena.
struct TTDFrameCache
{
    uint64_t frame = 0;
    std::vector<TTDFrameCacheEntry> entries;
    std::vector<TTDFrameCacheAccess> accesses;  ///< shared arena (cold, variable part)

    /// @brief Accesses performed by entry `i` (empty span if none).
    /// @return pointer to the first access (or nullptr) and the count.
    const TTDFrameCacheAccess* AccessesOf(size_t i, uint16_t& countOut) const
    {
        if (i >= entries.size()) { countOut = 0; return nullptr; }
        const TTDFrameCacheEntry& e = entries[i];
        countOut = e.accessCount;
        if (e.accessCount == 0 || e.accessOffset >= accesses.size())
            return nullptr;
        return &accesses[e.accessOffset];
    }

    /// @brief Heap footprint of this cache (records + arena), for telemetry.
    size_t Bytes() const
    {
        return sizeof(TTDFrameCache) + entries.capacity() * sizeof(TTDFrameCacheEntry) +
               accesses.capacity() * sizeof(TTDFrameCacheAccess);
    }
};

}  // namespace ttd
