#pragma once

/// @file ttd_dirty_tracker.h
/// @brief Per-physical-page dirty bitmap for TTD checkpoint capture.
///
/// Per parent TDD §6.2 and §6.3 (Phase 5 codec update):
///   - One bit per physical RAM page. **16 KB granularity** is kept here
///     on purpose: this is the smallest unit the emulator's Memory class
///     banks in, and write traps fire per 16 KB page. Sub-page splitting
///     happens later, at the codec-store level — see ttd_codec_page_store.h
///     (kPageSize = 4096, 4 sub-pages per emulator page) and the P-frame
///     path in TimeTravelManager::UpdateRamPages.
///   - Why not track at 4 KB here? Measured: per-page dirty cost is already
///     <1 % of the frame budget at 16 KB granularity, and 4 KB bit-tracking
///     would force the write-protect trap path in Memory to fire 4× more
///     often for no net win (the codec store already drops unchanged
///     sub-pages via InternXor's all-zero fast path).
///   - Two bitmaps: `_dirty` (per-frame, cleared by CollectAndClear) and
///     `_everDirty` (session-scoped, never cleared within a session).
///   - `_everDirty` distinguishes "never touched in this session" pages,
///     which carry the kNeverTouchedSlot sentinel across all 4 sub-page
///     slots in their TTDPageRef and cost nothing to capture or restore.
///   - MarkDirty is hot-path code: one predictable branch + one atomic OR
///     when the feature is enabled; zero cost when disabled.
///
/// Thread model: bitmaps are std::atomic<uint64_t>. MarkDirty is safe to
/// call from any thread (typically the emulator thread via
/// MemoryWriteDebug, but also the WebAPI IO thread via
/// DirectWriteToZ80Memory, or the Lua thread). CollectAndClear atomically
/// exchanges each word to 0 so concurrent MarkDirty calls are not lost
/// — the page will appear in the next frame's dirty set instead of the
/// current one, which is correct because the write happened after the
/// snapshot was taken.

#include <atomic>
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
    /// Called from Memory::MemoryWriteDebug (emulator thread) and
    /// Memory::DirectWriteToZ80Memory (WebAPI/Lua/snapshot-loader threads)
    /// on every RAM write when TTD is enabled.
    /// @param absPage  Physical RAM page index (0 .. MAX_RAM_PAGES-1).
    inline void MarkDirty(uint16_t absPage)
    {
        const uint64_t mask = (1ULL << (absPage & 63));
        // relaxed: we don't need cross-thread ordering with other memory;
        // we only need the bitmap word itself to be atomically OR'd so
        // a concurrent CollectAndClear (exchange) cannot lose the bit or
        // corrupt the word.
        _dirty[absPage >> 6].fetch_or(mask, std::memory_order_relaxed);
        _everDirty[absPage >> 6].fetch_or(mask, std::memory_order_relaxed);
    }

    /// @brief Test whether a page has ever been written in this session.
    /// Used by the capture path (TDD §6.3) to decide whether a page that is
    /// clean since the last checkpoint should still share the previous slot
    /// (it should) versus carry NEVER_TOUCHED (it shouldn't — it has been
    /// written at least once).
    inline bool WasEverDirty(uint16_t absPage) const
    {
        return (_everDirty[absPage >> 6].load(std::memory_order_relaxed)
                & (1ULL << (absPage & 63))) != 0;
    }

    /// @brief Test whether a page is dirty in the current frame window.
    inline bool IsDirty(uint16_t absPage) const
    {
        return (_dirty[absPage >> 6].load(std::memory_order_relaxed)
                & (1ULL << (absPage & 63))) != 0;
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
    // std::atomic<uint64_t> is the same size as uint64_t and is lock-free
    // on every platform we support (verified via ATOMIC_LLONG_LOCK_FREE,
    // which is 2 = always lock-free on x86-64/ARM64). The array elements
    // default-construct to 0 (std::atomic<uint64_t> default value).
    static_assert(ATOMIC_LLONG_LOCK_FREE == 2,
                  "atomic<uint64_t> must be always lock-free on this platform");
    std::atomic<uint64_t> _dirty[kWords];
    std::atomic<uint64_t> _everDirty[kWords];
};

} // namespace ttd
