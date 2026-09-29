#include "stdafx.h"

#include "ttdevosdcard.h"

#include <vector>

#include "emulator/ports/models/portdecoder_atm3.h"

namespace ttd
{

namespace
{
    // Layout: [0] version, [1] Z-Controller /CS, [2] receive latch, [3] reserved,
    // [4...] SdCardSpi::saveState (SdCardSpi::STATE_SIZE bytes)
    constexpr size_t kHeader = 4;
    constexpr uint8_t kVersion = 1;
}  // namespace

size_t TTDEvoSdCard::TTDStateSize() const
{
    return kHeader + SdCardSpi::STATE_SIZE;
}

void TTDEvoSdCard::TTDSaveState(uint8_t* dst) const
{
    const ZControllerSpi::State& zc = _decoder.GetZController().GetState();
    dst[0] = kVersion;
    dst[1] = zc.csN;
    dst[2] = zc.rxLatch;
    dst[3] = 0;
    _decoder.GetSdCard().saveState(dst + kHeader);
}

void TTDEvoSdCard::TTDLoadState(const uint8_t* src)
{
    if (src[0] != kVersion)
        return;
    // The card first: SetState re-announces the chip select to it
    _decoder.GetSdCard().loadState(src + kHeader);
    ZControllerSpi::State zc;
    zc.csN = src[1];
    zc.rxLatch = src[2];
    _decoder.GetZController().SetState(zc);
}

uint64_t TTDEvoSdCard::TTDHashState() const
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
