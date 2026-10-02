#include "stdafx.h"

#include "ttdevosdcard.h"

#include <vector>

#include "emulator/io/sdcard/sdcardspi.h"
#include "emulator/io/spi/zcontrollerspi.h"

namespace ttd
{

namespace
{
    // Layout: [0] version, [1] Z-Controller config byte, [2] receive latch,
    // [3] reserved, [4...] SdCardSpi::saveState (SdCardSpi::STATE_SIZE bytes).
    // Version 1 stored the SD /CS bit in [1] (1 = deselected); version 2 the
    // whole config byte, so the further chip selects of the TS-Conf VDAC2 build
    // (D2 = FT812) restore too. Same size, version 1 blobs still load
    constexpr size_t kHeader = 4;
    constexpr uint8_t kVersion = 2;
    constexpr uint8_t kVersionCsN = 1;
}  // namespace

size_t TTDEvoSdCard::TTDStateSize() const
{
    return kHeader + SdCardSpi::STATE_SIZE;
}

void TTDEvoSdCard::TTDSaveState(uint8_t* dst) const
{
    const ZControllerSpi::State& zc = _controller.GetState();
    dst[0] = kVersion;
    dst[1] = zc.config;
    dst[2] = zc.rxLatch;
    dst[3] = 0;
    _card.saveState(dst + kHeader);
}

void TTDEvoSdCard::TTDLoadState(const uint8_t* src)
{
    if (src[0] != kVersion && src[0] != kVersionCsN)
        return;
    // The card first: SetState re-announces the chip select to it
    _card.loadState(src + kHeader);
    ZControllerSpi::State zc;
    zc.config = src[0] == kVersion ? src[1] : static_cast<uint8_t>(src[1] ? ZControllerSpi::kResetConfig : 0x00);
    zc.rxLatch = src[2];
    _controller.SetState(zc);
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
