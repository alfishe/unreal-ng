#include "ttdatmpaging.h"

#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"

#include <cstring>

namespace ttd {

TTDAtmPaging::TTDAtmPaging(EmulatorContext* context) : _context(context) {}

AtmPagingState TTDAtmPaging::Snapshot() const
{
    AtmPagingState blob{};
    if (!_context)
        return blob;

    const EmulatorState& state = _context->emulatorState;

    // pFFF7 is `unsigned` in EmulatorState; copy element-wise so the blob keeps
    // a fixed 32-bit layout regardless of what `unsigned` is on the host.
    for (size_t i = 0; i < 8; ++i)
        blob.pFFF7[i] = static_cast<uint32_t>(state.pFFF7[i]);

    blob.aFF77 = static_cast<uint32_t>(state.aFF77);
    blob.pBD = state.pBD;
    blob.pBE = state.pBE;
    blob.pBF = state.pBF;
    blob.aFE = state.aFE;
    blob.aFB = state.aFB;
    blob.atmMemSwapped = state.atmMemSwapped ? 1 : 0;
    blob.cmos_addr = state.cmos_addr;

    return blob;
}

void TTDAtmPaging::TTDSaveState(uint8_t* dst) const
{
    if (!_context || !dst)
        return;

    const AtmPagingState blob = Snapshot();
    std::memcpy(dst, &blob, sizeof(blob));
}

void TTDAtmPaging::TTDLoadState(const uint8_t* src)
{
    if (!_context || !src)
        return;

    AtmPagingState blob{};
    std::memcpy(&blob, src, sizeof(blob));

    EmulatorState& state = _context->emulatorState;

    for (size_t i = 0; i < 8; ++i)
        state.pFFF7[i] = blob.pFFF7[i];

    state.aFF77 = blob.aFF77;
    state.pBD = blob.pBD;
    state.pBE = blob.pBE;
    state.pBF = blob.pBF;
    state.aFE = blob.aFE;
    state.aFB = blob.aFB;
    state.atmMemSwapped = blob.atmMemSwapped != 0;
    state.cmos_addr = blob.cmos_addr;

    // The caller re-runs the paging decode (Memory::UpdateZ80Banks) after every
    // serializer has loaded, so the restored map takes effect there rather than
    // here - see TimeTravelManager::RestoreCheckpoint.
}

uint64_t TTDAtmPaging::TTDHashState() const
{
    if (!_context)
        return 0;

    // Hash the same blob the capture path writes, byte-wise: a field added to
    // the blob then joins the divergence hash automatically.
    const AtmPagingState blob = Snapshot();

    uint64_t h = 0xcbf29ce484222325ULL;  // FNV-1a offset basis
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&blob);
    for (size_t i = 0; i < sizeof(blob); ++i)
    {
        h ^= static_cast<uint64_t>(bytes[i]);
        h *= 0x100000001b3ULL;           // FNV-1a prime
    }

    return h;
}

}  // namespace ttd
