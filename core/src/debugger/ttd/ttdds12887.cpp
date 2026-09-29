#include "stdafx.h"

#include "ttdds12887.h"

#include <array>

#include "emulator/io/rtc/ds12887.h"

namespace ttd
{

size_t TTDDs12887::TTDStateSize() const
{
    return Ds12887::kStateSize;
}

void TTDDs12887::TTDSaveState(uint8_t* dst) const
{
    _chip.SaveState(dst);
}

void TTDDs12887::TTDLoadState(const uint8_t* src)
{
    _chip.LoadState(src);
}

uint64_t TTDDs12887::TTDHashState() const
{
    std::array<uint8_t, Ds12887::kStateSize> blob{};
    _chip.SaveState(blob.data());
    uint64_t h = 0xcbf29ce484222325ULL;
    for (const uint8_t byte : blob)
    {
        h ^= byte;
        h *= 0x100000001b3ULL;
    }
    return h;
}

void TTDDs12887::TTDRecordingStarted()
{
    _chip.EnterEmulatedTime();
}

void TTDDs12887::TTDRecordingStopped()
{
    _chip.LeaveEmulatedTime();
}

}  // namespace ttd
