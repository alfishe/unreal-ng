#pragma once

/// @file ttd_dirty_tracker.h
/// @brief Per-physical-page dirty bitmap for TTD checkpoint capture.
///
/// Per parent TDD §6.2 and §6.3:
///   - One bit per physical RAM page (16 KB granularity).
///   - Two bitmaps: `_dirty` (per-frame, cleared by CollectAndClear) and
///     `_everDirty` (session-scoped, never cleared within a session).
///   - `_everDirty` distinguishes "never touched in this session" pages,
///     which carry the NEVER_TOUCHED sentinel in checkpoints and cost
///     nothing to capture or restore.
///   - MarkDirty is hot-path code: one predictable branch + one OR when
///     the feature is enabled; zero cost when disabled.
///
/// Thread model: touched only from the emulator thread (the same thread
/// that runs the CPU and the per-frame capture at OnFrameEnd). No
/// internal locking.

#include <cstdint>
#include <cstddef>
#include <vector>

#include "emulator/platform.h"  // MAX_RAM_PAGES

namespace ttd {

/// @brief Per-physical-RAM-page dirty tracking for TTD checkpoint capture.
///
/// Sized for the maximum supported model (TS-Conf / ZX-Evo, 256 pages = 4 MB).
/// Smaller models simply never touch the high bits — cost is identical.
class TTDDirtyTracker
{
public:
    /// Number of uint64 words needed to cover MAX_RAM_PAGES bits.
    static constexpr size_t kWords = (MAX_RAM_PAGES + 63) / 64;

    TTDDirtyTracker() = default;
    ~TTDDirtyTracker() = default;

    TTDDirtyTracker(const TTDDirtyTracker&) = delete;
    TTDDirtyTracker& operator=(const TTDDirtyTracker&) = delete;

    /// @brief Mark a physical RAM page as dirty.
    /// Called from Memory::MemoryWriteDebug on every RAM write when TTD is enabled.
    /// @param absPage  Physical RAM page index (0 .. MAX_RAM_PAGES-1).
    inline void MarkDirty(uint16_t absPage)
    {
        _dirty[absPage >> 6]     |= (1ULL << (absPage & 63));
        _everDirty[absPage >> 6] |= (1ULL << (absPage & 63));
    }

    /// @brief Test whether a page has ever been written in this session.
    /// Used by the capture path (TDD §6.3) to decide whether a page that is
    /// clean since the last checkpoint should still share the previous slot
    /// (it should) versus carry NEVER_TOUCHED (it shouldn't — it has been
    /// written at least once).
    inline bool WasEverDirty(uint16_t absPage) const
    {
        return (_everDirty[absPage >> 6] & (1ULL << (absPage & 63))) != 0;
    }

    /// @brief Test whether a page is dirty in the current frame window.
    inline bool IsDirty(uint16_t absPage) const
    {
        return (_dirty[absPage >> 6] & (1ULL << (absPage & 63))) != 0;
    }

    /// @brief Collect the set of pages dirty since the last CollectAndClear,
    /// in ascending page-index order (cache-friendly for the capture loop),
    /// and clear the per-frame dirty bitmap.
    ///
    /// The session-scoped `_everDirty` bitmap is NOT cleared — its whole
    /// purpose is to remember the ever-touched set across the session.
    ///
    /// @param outDirtyPages  Receiver; appended to (not cleared) so callers
    ///                       can reuse a single buffer across calls.
    void CollectAndClear(std::vector<uint16_t>& outDirtyPages);

    /// @brief Clear both bitmaps — used by `ttd clear` and by session
    /// invalidation (TDD §4.2: Reset / Load* / speed change / etc.).
    void ResetSession();

    /// @brief One-time reset of just the per-frame bitmap, without touching
    /// the ever-touched set. Currently unused outside of tests; documented
    /// for the future capture-at-arbitrary-point path (P2 seek engine may
    /// need it if a seek aborts mid-capture).
    void ClearFrameDirty();

    /// @brief Number of pages currently marked dirty in this frame window.
    /// Diagnostic / test helper; not used on the hot path.
    size_t DirtyCount() const;

    /// @brief Number of pages ever touched in this session.
    /// Diagnostic / test helper.
    size_t EverDirtyCount() const;

private:
    uint64_t _dirty[kWords]     = {0};
    uint64_t _everDirty[kWords] = {0};
};

} // namespace ttd
