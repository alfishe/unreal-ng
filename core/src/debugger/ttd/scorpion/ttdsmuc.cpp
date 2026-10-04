#include "stdafx.h"

#include "ttdsmuc.h"

#include <cstring>
#include <type_traits>

#include "emulator/emulatorcontext.h"
#include "emulator/ports/models/portdecoder_scorpion256.h"

namespace ttd
{

namespace
{
/// Blob layout: padding-free (copied and hashed byte-wise)
struct SmucBlob
{
    uint8_t version;
    uint8_t pFFBA;
    uint8_t p7FBA;
    uint8_t ideRegs[8];
    SMUCNvram::LinkState link;
};
static_assert(sizeof(SMUCNvram::LinkState) == 25, "SMUC link state layout drift");
static_assert(sizeof(SmucBlob) == 36, "SMUC TTD blob layout drift");
static_assert(std::is_trivially_copyable_v<SmucBlob>, "SMUC TTD blob must be POD");

SmucBlob Snapshot(PortDecoder_Scorpion256& decoder, const EmulatorContext& context)
{
    SmucBlob blob{};
    blob.version = TTDSmuc::kVersion;
    const EmulatorState& state = context.emulatorState;
    blob.pFFBA = state.pFFBA;
    blob.p7FBA = state.p7FBA;
    std::memcpy(blob.ideRegs, decoder.GetSmucIdeRegs(), sizeof(blob.ideRegs));
    blob.link = decoder.GetSMUCNvram().GetLinkState();
    return blob;
}
}  // namespace

size_t TTDSmuc::TTDStateSize() const
{
    return sizeof(SmucBlob);
}

void TTDSmuc::TTDSaveState(uint8_t* dst) const
{
    if (!dst)
        return;
    const SmucBlob blob = Snapshot(_decoder, _context);
    std::memcpy(dst, &blob, sizeof(blob));
}

void TTDSmuc::TTDLoadState(const uint8_t* src)
{
    if (!src || src[0] != kVersion)
        return;
    SmucBlob blob{};
    std::memcpy(&blob, src, sizeof(blob));
    EmulatorState& state = _context.emulatorState;
    state.pFFBA = blob.pFFBA;
    state.p7FBA = blob.p7FBA;
    std::memcpy(_decoder.GetSmucIdeRegs(), blob.ideRegs, sizeof(blob.ideRegs));
    _decoder.GetSMUCNvram().SetLinkState(blob.link);
}

uint64_t TTDSmuc::TTDHashState() const
{
    const SmucBlob blob = Snapshot(_decoder, _context);
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&blob);
    uint64_t h = 0xcbf29ce484222325ULL;  // FNV-1a
    for (size_t i = 0; i < sizeof(blob); ++i)
    {
        h ^= bytes[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

}  // namespace ttd
