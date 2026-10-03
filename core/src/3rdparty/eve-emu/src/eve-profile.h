#pragma once

// Profiling counters for offline analysis of captures (eve-replay-profile). Compiled
// only with EVE_PROFILE: the library itself carries no trace of them otherwise.
//
// Bitmap spans are counted by what decides their cost: format, filter, the path that
// drew them (the table-driven fast path or the general per-pixel path), the matrix
// (identity, axis-aligned scaling, rotation / shear) and the pixel pipeline (the
// default blend or anything else). Other primitives by kind. Times are host time.

#include <cstdint>

#ifdef EVE_PROFILE
#include <chrono>
#include <map>
#include <string>
#include <tuple>
#endif

namespace EveLib
{

#ifdef EVE_PROFILE

enum class ProfileMatrix : uint8_t { Identity, Scaled, Rotated };

struct ProfileBitmapKey
{
    uint8_t format = 0;
    uint8_t filter = 0;
    uint8_t fast = 0;
    ProfileMatrix matrix = ProfileMatrix::Identity;
    uint8_t defaultPipeline = 0;
    bool operator<(const ProfileBitmapKey& o) const
    {
        return std::tie(format, filter, fast, matrix, defaultPipeline) <
               std::tie(o.format, o.filter, o.fast, o.matrix, o.defaultPipeline);
    }
};

struct ProfileCell
{
    uint64_t calls = 0;
    uint64_t pixels = 0;
    uint64_t nanos = 0;
};

struct ProfileCounters
{
    std::map<ProfileBitmapKey, ProfileCell> bitmaps;
    std::map<uint8_t, ProfileCell> primitives;  // by BEGIN value, bitmaps excluded
    ProfileCell lines;                           // every drawn line: commands in `pixels`
    std::map<std::string, uint64_t> fastRejects; // why the fast path declined a span
    uint64_t opcodes[256] = {};                  // display list words executed, by opcode byte
};

ProfileCounters& Profile();
void ProfileReset();

inline uint64_t ProfileNow()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

#endif

} // namespace EveLib
