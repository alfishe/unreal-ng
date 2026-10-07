#include "stdafx.h"

#include "ttdevofontram.h"

#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"

#include <cstring>

namespace ttd
{

namespace
{
    // Layout: [0] version, [1..2048] the font RAM (code * 8 + row), [2049] the glyph byte #0EBD reads
    constexpr size_t kFontSize = 2048;
    constexpr size_t kSize = 1 + kFontSize + 1;
    constexpr uint8_t kVersion = 1;
}  // namespace

size_t TTDEvoFontRam::TTDStateSize() const
{
    return kSize;
}

void TTDEvoFontRam::TTDSaveState(uint8_t* dst) const
{
    const EmulatorState& state = _context->emulatorState;
    dst[0] = kVersion;
    std::memcpy(dst + 1, state.atm.fontRam, kFontSize);
    dst[1 + kFontSize] = state.atm.fontByte;
}

void TTDEvoFontRam::TTDLoadState(const uint8_t* src)
{
    if (src[0] != kVersion)
        return;
    EmulatorState& state = _context->emulatorState;
    std::memcpy(state.atm.fontRam, src + 1, kFontSize);
    state.atm.fontByte = src[1 + kFontSize];
}

uint64_t TTDEvoFontRam::TTDHashState() const
{
    uint8_t blob[kSize];
    TTDSaveState(blob);
    uint64_t h = 0xcbf29ce484222325ULL;
    for (const uint8_t byte : blob)
    {
        h ^= byte;
        h *= 0x100000001b3ULL;
    }
    return h;
}

}  // namespace ttd
