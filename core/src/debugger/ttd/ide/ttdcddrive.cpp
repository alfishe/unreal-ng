#include "stdafx.h"

#include "ttdcddrive.h"

#include <cstring>
#include <vector>

#include "emulator/emulatorcontext.h"
#include "emulator/io/ide/ata/atapicdrom.h"
#include "emulator/io/ide/idecontroller.h"

namespace ttd
{

namespace
{
    // Layout: [0] version, [1] CD unit mask, [2..7] reserved, then per unit
    // (channel * 2 + position) CdAudioState and AtapiStage, zero for a unit that is no CD drive
    constexpr size_t kHeader = 8;
    constexpr size_t kAudio = sizeof(CdAudioState);
    constexpr size_t kStage = sizeof(AtapiStage);
    constexpr size_t kUnit = kAudio + kStage;
    static_assert(kAudio == 32 && kStage == 2824, "CD drive state layout changed: bump TTDCdDrive::kVersion");

    AtapiCdrom* CdAt(EmulatorContext* context, int unit)
    {
        IdeController* ide = context ? context->pIdeController : nullptr;
        if (!ide || unit >= ide->ChannelCount() * AtaChannel::kUnits)
            return nullptr;
        AtaDevice* device = ide->Channel(unit / AtaChannel::kUnits).Unit(unit % AtaChannel::kUnits);
        return device && device->Kind() == AtaDeviceKind::Cdrom ? static_cast<AtapiCdrom*>(device) : nullptr;
    }
}  // namespace

TTDCdDrive::TTDCdDrive(EmulatorContext* context) : _context(context)
{
    IdeController* ide = context ? context->pIdeController : nullptr;
    _units = ide ? ide->ChannelCount() * AtaChannel::kUnits : 0;
}

size_t TTDCdDrive::TTDStateSize() const
{
    return kHeader + static_cast<size_t>(_units) * kUnit;
}

void TTDCdDrive::TTDSaveState(uint8_t* dst) const
{
    std::memset(dst, 0, TTDStateSize());
    dst[0] = kVersion;
    for (int unit = 0; unit < _units; unit++)
    {
        AtapiCdrom* cd = CdAt(_context, unit);
        if (!cd)
            continue;
        dst[1] = static_cast<uint8_t>(dst[1] | (1u << unit));
        uint8_t* at = dst + kHeader + static_cast<size_t>(unit) * kUnit;
        std::memcpy(at, &cd->Audio().State(), kAudio);
        std::memcpy(at + kAudio, &cd->Stage(), kStage);
    }
}

void TTDCdDrive::TTDLoadState(const uint8_t* src)
{
    if (src[0] != kVersion)
        return;
    for (int unit = 0; unit < _units; unit++)
    {
        AtapiCdrom* cd = CdAt(_context, unit);
        if (!cd || !((src[1] >> unit) & 1))
            continue;  // a unit this machine is not set up with as a CD drive
        const uint8_t* at = src + kHeader + static_cast<size_t>(unit) * kUnit;
        CdAudioState audio;
        std::memcpy(&audio, at, kAudio);
        cd->Audio().SetState(audio);
        AtapiStage stage;
        std::memcpy(&stage, at + kAudio, kStage);
        cd->SetStage(stage);
    }
}

uint64_t TTDCdDrive::TTDHashState() const
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
