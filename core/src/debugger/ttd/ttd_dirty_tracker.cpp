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
    for (size_t w = 0; w < kWords; ++w)
    {
        uint64_t bits = _dirty[w];
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

        _dirty[w] = 0;  // clear after enumerating; _everDirty untouched
    }
}

void TTDDirtyTracker::ResetSession()
{
    std::memset(_dirty,     0, sizeof(_dirty));
    std::memset(_everDirty, 0, sizeof(_everDirty));
}

void TTDDirtyTracker::ClearFrameDirty()
{
    std::memset(_dirty, 0, sizeof(_dirty));
}

size_t TTDDirtyTracker::DirtyCount() const
{
    size_t count = 0;
    for (size_t w = 0; w < kWords; ++w)
    {
        uint64_t x = _dirty[w];
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
        uint64_t x = _everDirty[w];
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
