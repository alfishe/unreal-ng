/// @file ttd_dirty_tracker.cpp
/// @brief TTDDirtyTracker non-inline helpers.
///
/// Per parent TDD §6.2 and §6.3. The hot-path MarkDirty is inlined in the
/// header; this file holds CollectAndClear (called once per frame from the
/// capture orchestrator) and the Reset / diagnostic helpers used by tests
/// and by session lifecycle (ttd clear, invalidation hooks).

#include "ttd_dirty_tracker.h"

#include <cstring>  // memset

namespace ttd
{

void TTDDirtyTracker::CollectAndClear(std::vector<uint16_t>& outDirtyPages)
{
    // Walk the bitmap word-by-word; for each set bit, emit the page index.
    // Word order is ascending, so emitted page indices are ascending too —
    // cache-friendly for the capture loop that follows.
    //
    // Each word is atomically exchanged with 0. This means a concurrent
    // MarkDirty(page) call from another thread can only affect one of two
    // outcomes: (a) the bit lands in `bits` here and the page is captured
    // this frame; (b) the bit lands AFTER the exchange and stays set in
    // _dirty for the next frame's CollectAndClear. Both are correct.
    // Without exchange, a torn read-modify-write race could lose bits.
    for (size_t w = 0; w < kWords; ++w)
    {
        uint64_t bits = _dirty[w].exchange(0, std::memory_order_relaxed);
        if (bits == 0)
            continue;

        // Could use __builtin_ctzll in a loop for sparse bitmaps, but the
        // typical dirty set is small (2–6 pages per frame per TDD §6.3) and
        // this loop terminates quickly when bits is exhausted.
        while (bits != 0)
        {
            unsigned long bitIndex;
#ifdef _MSC_VER
            _BitScanForward64(&bitIndex, bits);
#else
            bitIndex = static_cast<unsigned long>(__builtin_ctzll(bits));
#endif
            uint16_t page = static_cast<uint16_t>((w << 6) | bitIndex);
            outDirtyPages.push_back(page);
            bits &= (bits - 1);  // clear lowest set bit
        }
        // Word already cleared by exchange above; _everDirty untouched.
    }
}

void TTDDirtyTracker::ResetSession()
{
    // atomically set each word to 0. store with relaxed ordering is fine —
    // callers (StartRecording / InvalidateSession) have already paused the
    // emulator and serialized against WebAPI mutations via
    // PauseAndConfirm, so this is defensive rather than contended.
    for (size_t w = 0; w < kWords; ++w)
    {
        _dirty[w].store(0, std::memory_order_relaxed);
        _everDirty[w].store(0, std::memory_order_relaxed);
    }
}

void TTDDirtyTracker::ClearFrameDirty()
{
    for (size_t w = 0; w < kWords; ++w)
    {
        _dirty[w].store(0, std::memory_order_relaxed);
    }
}

size_t TTDDirtyTracker::DirtyCount() const
{
    size_t count = 0;
    for (size_t w = 0; w < kWords; ++w)
    {
        uint64_t x = _dirty[w].load(std::memory_order_relaxed);
#if defined(__GNUC__) || defined(__clang__)
        count += __builtin_popcountll(x);
#elif defined(_MSC_VER)
        count += __popcnt64(x);
#else
        // Fallback bit-by-bit
        while (x) { count += (x & 1); x >>= 1; }
#endif
    }
    return count;
}

size_t TTDDirtyTracker::EverDirtyCount() const
{
    size_t count = 0;
    for (size_t w = 0; w < kWords; ++w)
    {
        uint64_t x = _everDirty[w].load(std::memory_order_relaxed);
#if defined(__GNUC__) || defined(__clang__)
        count += __builtin_popcountll(x);
#elif defined(_MSC_VER)
        count += __popcnt64(x);
#else
        while (x) { count += (x & 1); x >>= 1; }
#endif
    }
    return count;
}

} // namespace ttd
