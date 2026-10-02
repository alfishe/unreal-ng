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
    // [4..11] IdeAdapterState, then AtaDeviceState of unit 0 and unit 1.
    // A two-channel board (the Sprinter) appends the second channel the same way:
    // [selected, kind 0, kind 1, 0] and its two AtaDeviceStates; one-channel blobs are unchanged
    constexpr size_t kHeader = 4;
    constexpr size_t kAdapter = sizeof(IdeAdapterState);
    constexpr size_t kUnit = sizeof(AtaDeviceState);
    constexpr size_t kChannel = kHeader + 2 * kUnit;
    constexpr uint8_t kVersion = 1;
    static_assert(kAdapter == 8, "IdeAdapterState layout changed: bump kVersion");

    int Channels(EmulatorContext* context)
    {
        IdeController* ide = context ? context->pIdeController : nullptr;
        return ide && ide->ChannelCount() == 2 ? 2 : 1;
    }

    /// Selected unit, unit kinds, and the units' states of one channel: header at `header`, units at `units`
    void SaveChannel(AtaChannel& channel, uint8_t* header, uint8_t* units)
    {
        header[0] = static_cast<uint8_t>(channel.SelectedState());
        for (int unit = 0; unit < 2; unit++)
        {
            if (AtaDevice* device = channel.Unit(unit))
            {
                header[1 + unit] = static_cast<uint8_t>(device->Kind());
                std::memcpy(units + unit * kUnit, &device->State(), kUnit);
            }
        }
    }

    void LoadChannel(AtaChannel& channel, const uint8_t* header, const uint8_t* units)
    {
        channel.SetSelectedState(header[0]);
        for (int unit = 0; unit < 2; unit++)
        {
            AtaDevice* device = channel.Unit(unit);
            if (!device || header[1 + unit] != static_cast<uint8_t>(device->Kind()))
                continue;  // a unit this machine is not set up with: nothing to restore into
            AtaDeviceState state;
            std::memcpy(&state, units + unit * kUnit, kUnit);
            device->SetState(state);
        }
    }
}  // namespace

size_t TTDAtaChannel::TTDStateSize() const
{
    return kHeader + kAdapter + 2 * kUnit + (Channels(_context) == 2 ? kChannel : 0);
}

void TTDAtaChannel::TTDSaveState(uint8_t* dst) const
{
    std::memset(dst, 0, TTDStateSize());
    dst[0] = kVersion;
    IdeController* ide = _context ? _context->pIdeController : nullptr;
    if (!ide)
        return;
    SaveChannel(ide->Channel(0), dst + 1, dst + kHeader + kAdapter);
    if (Channels(_context) == 2)
    {
        uint8_t* second = dst + kHeader + kAdapter + 2 * kUnit;
        SaveChannel(ide->Channel(1), second, second + kHeader);
    }
    if (_context->pPortDecoder)
        std::memcpy(dst + kHeader, &_context->pPortDecoder->GetIdeAdapter().State(), kAdapter);
}

void TTDAtaChannel::TTDLoadState(const uint8_t* src)
{
    IdeController* ide = _context ? _context->pIdeController : nullptr;
    if (src[0] != kVersion || !ide)
        return;
    LoadChannel(ide->Channel(0), src + 1, src + kHeader + kAdapter);
    if (Channels(_context) == 2)
    {
        const uint8_t* second = src + kHeader + kAdapter + 2 * kUnit;
        LoadChannel(ide->Channel(1), second, second + kHeader);
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
