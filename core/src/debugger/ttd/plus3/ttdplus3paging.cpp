#include "ttdplus3paging.h"

#include "emulator/emulatorcontext.h"
#include "emulator/video/ulacontention.h"

#include <cstring>

namespace ttd
{
TTDPlus3Paging::TTDPlus3Paging(EmulatorContext* context) : _context(context)
{
}

Plus3PagingState TTDPlus3Paging::Snapshot() const
{
    Plus3PagingState blob{};
    blob.p1FFD = _context->emulatorState.p1FFD;
    if (const UlaContention* ula = _context->pUlaContention)
    {
        blob.floatingBus = ula->GetLatchedByte();
        blob.flags = 0x01;
    }
    return blob;
}

void TTDPlus3Paging::TTDSaveState(uint8_t* dst) const
{
    if (!_context || !dst)
        return;

    const Plus3PagingState blob = Snapshot();
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
    if ((blob.flags & 0x01) && _context->pUlaContention)
        _context->pUlaContention->LatchContendedByte(blob.floatingBus);
}

uint64_t TTDPlus3Paging::TTDHashState() const
{
    if (!_context)
        return 0;

    const Plus3PagingState blob = Snapshot();
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&blob);
    uint64_t h = 0xcbf29ce484222325ULL;  // FNV-1a offset basis
    for (size_t i = 0; i < sizeof(blob); ++i)
    {
        h ^= static_cast<uint64_t>(bytes[i]);
        h *= 0x100000001b3ULL;           // FNV-1a prime
    }
    return h;
}
} // namespace ttd
