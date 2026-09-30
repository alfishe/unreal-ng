#pragma once

/// @file videofamily.h
/// @brief Which renderer family draws a video mode. One switch for the renderer
/// (ScreenZX::SelectRangeRenderer) and the video debug mappers
/// (VideoMapService), so the debug description always follows what is drawn
/// (PLAN #42 design G1, G8).

#include <cstdint>

#include "emulator/video/screen.h"

enum class VideoFamily : uint8_t
{
    None,   ///< M_NUL: nothing is drawn
    Zx,     ///< Sinclair / Pentagon ULA layout - also every mode without its own renderer yet
    Alco,   ///< Pentagon 1024 / ZX-Evo AlCo 16c and hardware multicolor
    Atm,    ///< ATM Turbo 2+ / ATM3 / ZX-Evo extended modes
    Profi,  ///< Profi 512x240 hi-res
    TsConf, ///< TS-Conf (ScreenTSConf draws every mode; TsConfVideoMapper maps its graphics layer)
};

constexpr VideoFamily FamilyOf(VideoModeEnum mode)
{
    switch (mode)
    {
        case M_NUL:
            return VideoFamily::None;
        case M_ATM16:
        case M_ATMHR:
        case M_ATMTX:
        case M_ATMTL:
            return VideoFamily::Atm;
        case M_PROFIHR:
            return VideoFamily::Profi;
        case M_TS16:
        case M_TS256:
        case M_TSTX:
        case M_TSZX:
            return VideoFamily::TsConf;
        case M_P16:
        case M_PMC:
            return VideoFamily::Alco;
        default:
            return VideoFamily::Zx;
    }
}
