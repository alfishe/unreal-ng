#include "ttdgmxpaging.h"

#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"
#include "emulator/ports/models/portdecoder_scorpiongmx.h"

#include <cstring>

namespace ttd {

TTDGmxPaging::TTDGmxPaging(EmulatorContext* context) : _context(context)
{
}

GmxPagingState TTDGmxPaging::Snapshot() const
{
    GmxPagingState blob{};
    blob.version = kVersion;
    if (!_context)
        return blob;

    const EmulatorState& state = _context->emulatorState;
    const GmxState& gmx = state.gmx;
    blob.p00 = gmx.p00;
    blob.p78FD = gmx.p78FD;
    blob.p7AFD = gmx.p7AFD;
    blob.p7CFD = gmx.p7CFD;
    blob.pDFFD = state.pDFFD;
    blob.magicShift = gmx.magicShift;
    blob.magicLock = gmx.magicLock;
    blob.lock7AFD = gmx.lock7AFD;
    blob.lock7EFD = gmx.lock7EFD;
    const auto* decoder = dynamic_cast<const PortDecoder_ScorpionGMX*>(_context->pPortDecoder);
    blob.reconfPending = (decoder && decoder->IsReconfigurationPending()) ? 1 : 0;
    return blob;
}

void TTDGmxPaging::TTDSaveState(uint8_t* dst) const
{
    if (!dst)
        return;

    const GmxPagingState blob = Snapshot();
    std::memcpy(dst, &blob, sizeof(blob));
}

void TTDGmxPaging::TTDLoadState(const uint8_t* src)
{
    if (!_context || !src)
        return;

    GmxPagingState blob{};
    std::memcpy(&blob, src, sizeof(blob));

    EmulatorState& state = _context->emulatorState;
    GmxState& gmx = state.gmx;
    gmx.p00 = blob.p00;
    gmx.p78FD = blob.p78FD;
    gmx.p7AFD = blob.p7AFD;
    gmx.p7CFD = blob.p7CFD;
    state.pDFFD = blob.pDFFD;
    gmx.magicShift = blob.magicShift;
    gmx.magicLock = blob.magicLock;
    gmx.lock7AFD = blob.lock7AFD;
    gmx.lock7EFD = blob.lock7EFD;

    // The pending RECONF and its step hook; the caller rebuilds the banks (UpdateZ80Banks) from the latches
    if (auto* decoder = dynamic_cast<PortDecoder_ScorpionGMX*>(_context->pPortDecoder))
        decoder->RestoreReconfigurationPending(blob.reconfPending != 0);
}

uint64_t TTDGmxPaging::TTDHashState() const
{
    const GmxPagingState blob = Snapshot();

    uint64_t h = 0xcbf29ce484222325ULL;  // FNV-1a offset basis
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&blob);
    for (size_t i = 0; i < sizeof(blob); ++i)
    {
        h ^= static_cast<uint64_t>(bytes[i]);
        h *= 0x100000001b3ULL;  // FNV-1a prime
    }
    return h;
}

}  // namespace ttd
