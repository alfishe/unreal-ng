#pragma once

/// @file ttdscreenlayout.h
/// @brief The picture's bytes in a session without the captured screen layouts (a session loaded from a file): the
/// ULA models show one RAM page (5, or 7 by the 7FFD latch). Shared by the timeline (TD-5) and find-event.

#include <cstdint>
#include <vector>

#include "debugger/ttd/engine/ttdframeinput.h"   // TTDScreenRange
#include "emulator/platform.h"                  // MEM_MODEL
#include "emulator/video/screen.h"

namespace ttd
{

/// Models whose picture is the ZX screen of one RAM page: a session without the captured screen layouts still finds
/// their screen from the 7FFD latch; the others have no screen there
inline bool HasUlaScreen(MEM_MODEL model)
{
    switch (model)
    {
        case MM_SPECTRUM48:
        case MM_SPECTRUM128:
        case MM_PLUS2:
        case MM_PLUS2A:
        case MM_PLUS3:
        case MM_PENTAGON:
        case MM_SCORP:
        case MM_PROFSCORP:
            return true;
        default:
            return false;
    }
}

/// The ULA picture (6912 bytes of page 5, or of page 7) in the RAM region @p ramRegion, by the 7FFD latch
inline std::vector<TTDScreenRange> UlaScreenLayout(uint32_t ramRegion, MEM_MODEL model, uint8_t p7FFD)
{
    const uint32_t page = Screen::GetVideoRAMPage(model, p7FFD) == 7 ? 7u : 5u;
    return {{ramRegion, page * 0x4000u, page * 0x4000u + 6912u}};
}

}  // namespace ttd
