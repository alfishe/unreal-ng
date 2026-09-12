#include "ttdscorpionprofrom.h"

#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

#include <cstring>

namespace ttd {

TTDScorpionProfROM::TTDScorpionProfROM(EmulatorContext* context)
    : _context(context)
{
}

uint8_t TTDScorpionProfROM::CurrentRomPage() const
{
    if (!_context || !_context->pMemory)
        return kNoRomPage;

    // Must gate on IsBank0ROM() rather than on GetROMPage() returning
    // MEMORY_UNMAPPABLE: in release builds GetROMPageFromAddress() skips its
    // range check and does unchecked pointer arithmetic, so with RAM latched at
    // #0000 (#1FFD bit 0) it returns a garbage page instead of the sentinel.
    if (!_context->pMemory->IsBank0ROM())
        return kNoRomPage;

    // The ProfROM bundle lays each 64K plane out as 4 consecutive 16K pages,
    // so the low 2 bits of the bundle page are the page within the plane.
    const uint16_t page = _context->pMemory->GetROMPage();
    if (page == MEMORY_UNMAPPABLE)
        return kNoRomPage;

    return static_cast<uint8_t>(page & 0x03);
}

ScorpionProfROMState TTDScorpionProfROM::Snapshot() const
{
    ScorpionProfROMState blob{};
    if (!_context)
        return blob;

    const EmulatorState& state = _context->emulatorState;

    blob.plane_id = state.profrom_bank;
    blob.rom_page = CurrentRomPage();
    blob.p7EFD = state.p7EFD;
    blob.p1FFD = state.p1FFD;
    blob.scorpionDosTrigger = state.scorpionDosTrigger;

    return blob;
}

void TTDScorpionProfROM::TTDSaveState(uint8_t* dst) const
{
    if (!_context || !dst)
        return;

    const ScorpionProfROMState blob = Snapshot();
    std::memcpy(dst, &blob, sizeof(blob));
}

void TTDScorpionProfROM::TTDLoadState(const uint8_t* src)
{
    if (!_context || !src)
        return;

    ScorpionProfROMState blob{};
    std::memcpy(&blob, src, sizeof(blob));

    EmulatorState& state = _context->emulatorState;

    state.profrom_bank = blob.plane_id;
    state.p7EFD = blob.p7EFD;
    state.p1FFD = blob.p1FFD;
    state.scorpionDosTrigger = blob.scorpionDosTrigger;

    // rom_page is deliberately NOT written back: it is a derived observation of
    // the paging chain, which the caller rebuilds (UpdateZ80Banks) from the
    // latches restored above. Forcing it here would mask a paging bug instead
    // of surfacing it as a divergence.
}

uint64_t TTDScorpionProfROM::TTDHashState() const
{
    if (!_context)
        return 0;

    // Hash the same blob the capture path writes, byte-wise. Deriving both from
    // Snapshot() means a field added to the blob automatically joins the hash.
    const ScorpionProfROMState blob = Snapshot();

    uint64_t h = 0xcbf29ce484222325ULL;  // FNV-1a offset basis
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&blob);
    for (size_t i = 0; i < sizeof(blob); ++i)
    {
        h ^= static_cast<uint64_t>(bytes[i]);
        h *= 0x100000001b3ULL;           // FNV-1a prime
    }

    return h;
}

} // namespace ttd
