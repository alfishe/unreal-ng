#include "ttdplus3paging.h"

#include "emulator/emulatorcontext.h"

#include <cstring>

namespace ttd
{
TTDPlus3Paging::TTDPlus3Paging(EmulatorContext* context) : _context(context)
{
}

void TTDPlus3Paging::TTDSaveState(uint8_t* dst) const
{
    if (!_context || !dst)
        return;

    Plus3PagingState blob{};
    blob.p1FFD = _context->emulatorState.p1FFD;
    std::memcpy(dst, &blob, sizeof(blob));
}

void TTDPlus3Paging::TTDLoadState(const uint8_t* src)
{
    if (!_context || !src)
        return;

    Plus3PagingState blob{};
    std::memcpy(&blob, src, sizeof(blob));

    // The latch only: the caller rebuilds the banks (UpdateZ80Banks) from it
    _context->emulatorState.p1FFD = blob.p1FFD;
}

uint64_t TTDPlus3Paging::TTDHashState() const
{
    if (!_context)
        return 0;

    uint64_t h = 0xcbf29ce484222325ULL;  // FNV-1a offset basis
    h ^= static_cast<uint64_t>(_context->emulatorState.p1FFD);
    h *= 0x100000001b3ULL;               // FNV-1a prime
    return h;
}
} // namespace ttd
