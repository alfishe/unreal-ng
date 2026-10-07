#include "ttdatmpaging.h"

#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"
#include "emulator/ports/models/portdecoder_atm3.h"
#include "emulator/ports/models/portdecoder_atm710.h"

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
        blob.pFFF7[i] = static_cast<uint32_t>(state.atm.pFFF7[i]);

    blob.aFF77 = static_cast<uint32_t>(state.atm.aFF77);
    blob.pBD = state.evo.pBD;
    blob.pBE = state.evo.pBE;
    blob.pBF = state.evo.pBF;
    blob.aFE = state.atm.aFE;
    blob.aFB = state.atm.aFB;
    blob.pFDFD = state.pFDFD;
    blob.atmMemSwapped = state.atm.memSwapped ? 1 : 0;
    if (auto* atm3 = dynamic_cast<PortDecoder_ATM3*>(_context->pPortDecoder))
        atm3->GetEvoAvr().GetVolatileState(blob.evoAvrExtType, blob.evoAvrEepromPage, blob.evoAvrFlags);

    for (size_t i = 0; i < 16; ++i)
    {
        blob.atmPalette[i] = state.atm.palette[i];
        blob.atmPaletteRegs[i] = state.atm.paletteRegs[i];
    }
    blob.atmBorderBright = state.atm.borderBright;
    blob.evoFddMask = state.evo.fddMask;
    blob.evoInNmi = state.evo.inNmi ? 1 : 0;
    blob.evoNmiEntry = state.evo.nmiEntry ? 1 : 0;
    blob.nmiAtIntPending = state.nmiAtIntStartPending ? 1 : 0;
    blob.evoTrdemu = state.evo.trdemu;
    blob.evoVgSys = state.evo.vgSys;
    blob.evoWrProt = state.evo.wrProt;
    blob.evoTurboPending = state.evo.turboPending;

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
        state.atm.pFFF7[i] = blob.pFFF7[i];

    state.atm.aFF77 = blob.aFF77;
    state.evo.pBD = blob.pBD;
    state.evo.pBE = blob.pBE;
    state.evo.pBF = blob.pBF;
    state.atm.aFE = blob.aFE;
    state.atm.aFB = blob.aFB;
    state.pFDFD = blob.pFDFD;
    state.atm.memSwapped = blob.atmMemSwapped != 0;
    if (auto* atm3 = dynamic_cast<PortDecoder_ATM3*>(_context->pPortDecoder))
        atm3->GetEvoAvr().SetVolatileState(blob.evoAvrExtType, blob.evoAvrEepromPage, blob.evoAvrFlags);
    // The v7.10 board's 7 MHz RAM waits follow #FF77 bit 3, restored with the core state (no-op on the ZX-Evo)
    if (auto* atm710 = dynamic_cast<PortDecoder_ATM710*>(_context->pPortDecoder))
        atm710->SyncTurboRamWaits();

    for (size_t i = 0; i < 16; ++i)
    {
        state.atm.palette[i] = blob.atmPalette[i];
        state.atm.paletteRegs[i] = blob.atmPaletteRegs[i];
    }
    state.atm.borderBright = blob.atmBorderBright;
    state.evo.fddMask = blob.evoFddMask;
    state.evo.inNmi = blob.evoInNmi != 0;
    state.evo.nmiEntry = blob.evoNmiEntry != 0;
    state.nmiAtIntStartPending = blob.nmiAtIntPending != 0;
    state.evo.trdemu = blob.evoTrdemu;
    state.evo.vgSys = blob.evoVgSys;
    state.evo.wrProt = blob.evoWrProt;
    state.evo.turboPending = blob.evoTurboPending;
    // #BF bit 2 decides whether the font RAM loader sits on the bus
    if (auto* atm3 = dynamic_cast<PortDecoder_ATM3*>(_context->pPortDecoder))
        atm3->SyncFontOverlay();

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
