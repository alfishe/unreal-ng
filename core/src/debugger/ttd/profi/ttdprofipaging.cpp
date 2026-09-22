#include "ttdprofipaging.h"

#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"

#include <cstring>

namespace ttd {

TTDProfiPaging::TTDProfiPaging(EmulatorContext* context) : _context(context) {}

ProfiPagingState TTDProfiPaging::Snapshot() const
{
    ProfiPagingState blob{};
    if (!_context)
        return blob;

    const EmulatorState& state = _context->emulatorState;

    blob.pDFFD = state.pDFFD;
    std::memcpy(blob.profiPalette, state.profiPalette, sizeof(blob.profiPalette));

    return blob;
}

void TTDProfiPaging::TTDSaveState(uint8_t* dst) const
{
    if (!_context || !dst)
        return;

    const ProfiPagingState blob = Snapshot();
    std::memcpy(dst, &blob, sizeof(blob));
}

void TTDProfiPaging::TTDLoadState(const uint8_t* src)
{
    if (!_context || !src)
        return;

    ProfiPagingState blob{};
    std::memcpy(&blob, src, sizeof(blob));

    EmulatorState& state = _context->emulatorState;
    state.pDFFD = blob.pDFFD;
    std::memcpy(state.profiPalette, blob.profiPalette, sizeof(blob.profiPalette));

    // The caller re-runs the paging decode (Memory::UpdateZ80Banks) after every
    // serializer has loaded, so the restored map takes effect there - see
    // TimeTravelManager::RestoreCheckpoint. The video mode is re-derived from
    // pDFFD by Screen::InitRaster on the next frame.
}

uint64_t TTDProfiPaging::TTDHashState() const
{
    if (!_context)
        return 0;

    const ProfiPagingState blob = Snapshot();

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
