#include "stdafx.h"

#include "ttdwd1793context.h"

#include <vector>

#include "emulator/io/fdc/wd1793.h"

namespace ttd
{

size_t TTDWd1793Context::TTDStateSize() const
{
    return 1 + WD1793::kTransferContextSize;
}

void TTDWd1793Context::TTDSaveState(uint8_t* dst) const
{
    if (!dst)
        return;
    dst[0] = kVersion;
    _fdc.SaveTransferContext(dst + 1);
}

void TTDWd1793Context::TTDLoadState(const uint8_t* src)
{
    if (!src || src[0] != kVersion)
        return;
    _fdc.LoadTransferContext(src + 1);
}

uint64_t TTDWd1793Context::TTDHashState() const
{
    std::vector<uint8_t> blob(TTDStateSize());
    TTDSaveState(blob.data());
    uint64_t h = 0xcbf29ce484222325ULL;
    for (uint8_t b : blob)
        h = (h ^ b) * 0x100000001b3ULL;
    return h;
}

}  // namespace ttd
