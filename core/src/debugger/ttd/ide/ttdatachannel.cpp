#include "stdafx.h"

#include "ttdatachannel.h"

#include <cstring>
#include <vector>

#include "emulator/emulatorcontext.h"
#include "emulator/io/ide/idecontroller.h"
#include "emulator/ports/portdecoder.h"

namespace ttd
{

namespace
{
    // Layout: [0] version, [1] selected unit, [2] [3] unit kinds (0 none, 1 disk, 2 CD),
    // [4..11] IdeAdapterState, then AtaDeviceState of unit 0 and unit 1
    constexpr size_t kHeader = 4;
    constexpr size_t kAdapter = sizeof(IdeAdapterState);
    constexpr size_t kUnit = sizeof(AtaDeviceState);
    constexpr uint8_t kVersion = 1;
    static_assert(kAdapter == 8, "IdeAdapterState layout changed: bump kVersion");
}  // namespace

size_t TTDAtaChannel::TTDStateSize() const
{
    return kHeader + kAdapter + 2 * kUnit;
}

void TTDAtaChannel::TTDSaveState(uint8_t* dst) const
{
    std::memset(dst, 0, TTDStateSize());
    dst[0] = kVersion;
    IdeController* ide = _context ? _context->pIdeController : nullptr;
    if (!ide)
        return;
    AtaChannel& channel = ide->Channel();
    dst[1] = static_cast<uint8_t>(channel.SelectedState());
    for (int unit = 0; unit < 2; unit++)
    {
        if (AtaDevice* device = channel.Unit(unit))
        {
            dst[2 + unit] = static_cast<uint8_t>(device->Kind());
            std::memcpy(dst + kHeader + kAdapter + unit * kUnit, &device->State(), kUnit);
        }
    }
    if (_context->pPortDecoder)
        std::memcpy(dst + kHeader, &_context->pPortDecoder->GetIdeAdapter().State(), kAdapter);
}

void TTDAtaChannel::TTDLoadState(const uint8_t* src)
{
    IdeController* ide = _context ? _context->pIdeController : nullptr;
    if (src[0] != kVersion || !ide)
        return;
    AtaChannel& channel = ide->Channel();
    channel.SetSelectedState(src[1]);
    for (int unit = 0; unit < 2; unit++)
    {
        AtaDevice* device = channel.Unit(unit);
        if (!device || src[2 + unit] != static_cast<uint8_t>(device->Kind()))
            continue;  // a unit this machine is not set up with: nothing to restore into
        AtaDeviceState state;
        std::memcpy(&state, src + kHeader + kAdapter + unit * kUnit, kUnit);
        device->SetState(state);
    }
    if (_context->pPortDecoder)
    {
        IdeAdapterState latches;
        std::memcpy(&latches, src + kHeader, kAdapter);
        _context->pPortDecoder->GetIdeAdapter().SetState(latches);
    }
}

uint64_t TTDAtaChannel::TTDHashState() const
{
    std::vector<uint8_t> blob(TTDStateSize());
    TTDSaveState(blob.data());
    uint64_t h = 0xcbf29ce484222325ULL;
    for (const uint8_t byte : blob)
    {
        h ^= byte;
        h *= 0x100000001b3ULL;
    }
    return h;
}

}  // namespace ttd
