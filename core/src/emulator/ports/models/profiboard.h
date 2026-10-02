#pragma once

/// @file profiboard.h
/// @brief What differs between the two Profi boards, in one place.
///
/// The Profi came as two board families (design: docs/inprogress/2026-10-01-profi-v3-v5):
///
/// | Board | Model | Differences from the other |
/// |:--|:--|:--|
/// | v3.x (TOO "Profi" / JV Kramis, 1990) | `MM_PROFI3` | monochrome 512x240; the port decoder PROM takes ADR15 where v5's takes ROM14, so there is no extended port map, no RTC, no IDE |
/// | v5.0x (Kondor, 1993-94) | `MM_PROFI` | 16-colour 512x240 from a 256-colour palette at #xx7E; the extended map with CP/M + ROM14 (FDC #83..#E3, system #3F, 8255 #87..#E7, IDE, RTC); #FE bit 7 from the palette |
///
/// Everything else - #7FFD / #DFFD paging, the DOS latch, the SYS ROM, the CP/M map (#1F..#7F + #BF), the 8255 and
/// its Covox at #3F / #5F, the mouse - is the same on both, and code that serves both keys on IsProfiModel().
/// Each field is one difference, so a port decoder arm or a renderer asks one question instead of comparing models.

#include <cstdint>

#include "emulator/platform.h"

struct ProfiBoard
{
    bool palette = false;        ///< #xx7E palette writes in DS80; 16-colour hi-res (else monochrome)
    bool extendedPorts = false;  ///< the CP/M + ROM14 port map with RTC, IDE and the 8255 at #87..#E7
    bool fePaletteBit7 = false;  ///< #FE read bit 7 comes from the palette in DS80 (else it reads 1)

    /// The board of `model`; a non-Profi model gets a board with nothing (all false)
    static constexpr ProfiBoard For(MEM_MODEL model)
    {
        ProfiBoard board;
        if (model == MM_PROFI)
        {
            board.palette = true;
            board.extendedPorts = true;
            board.fePaletteBit7 = true;
        }
        return board;
    }
};

/// The hi-res mode draws ink = border colour, paper = its inverse, without the attribute page: always on v3, and on
/// a v5 board fitted without its palette chips ([ULA] ProfiMonochrome=1, the v5.0 album's "PROFI+ V4.02" colours)
inline bool ProfiMonochromeHires(const CONFIG& config)
{
    return !ProfiBoard::For(config.mem_model).palette || config.profi_monochrome != 0;
}

/// The video sync PROM a Profi board carries. Boards shipped with different ones, and they give different frames
/// (docs/inprogress/2026-10-01-profi-v3-v5/cross-check.md section 4, decoded by tools/machines/profi/syncprom).
/// Chosen with [PROFI] SyncProm=; Default is the board's own (v3: Vr0a1d, v5: V503)
enum class ProfiSyncProm : uint8_t
{
    Default = 0,
    Vr0a1d,     ///< "0a1d": 0A1DFAFD, the original PROM of a v3.2 board (MDESK dump) - 69888 T, INT 12580 T before paper
    Samx6,      ///< "samx6": 15E9B638, Kondor's SAMX6 for v3/4 boards - 69888 T, 12592 T
    Fb0579b6,   ///< "fb0579b6": FB0579B6, the v3.2 Kramis board xpeccy-plus measured - 71680 T, INT 48 T before paper
    V503,       ///< "v503": D2D4A7C8, read off a Kondor 5.04 board, with the DD53 reload fix - 69888 T, 14368 T
};

/// The frame a sync PROM gives, in 3.5 MHz T-states
struct ProfiFrame
{
    uint32_t frame;        ///< T-states per frame
    uint32_t tLine;        ///< T-states per line
    uint32_t intToPaper;   ///< from the INT edge to the first paper dot
    uint32_t intLength;    ///< INT length
};

/// The board's PROM when `prom` is Default
constexpr ProfiSyncProm ProfiResolveSyncProm(ProfiSyncProm prom, MEM_MODEL model)
{
    if (prom != ProfiSyncProm::Default)
        return prom;
    return model == MM_PROFI3 ? ProfiSyncProm::Vr0a1d : ProfiSyncProm::V503;
}

/// The frame of `prom` (Default resolves through the board). Every row keeps the 312-line raster of the
/// renderer: the paper starts at line 72, T 24 (T 16152 of the frame); a 320-line frame only adds 8 lines the
/// raster does not show. INT lengths: v5 28 T (K. Gromov tuned it to 8-8.6 us, ZX-Ревю 1996), elsewhere 32 T until
/// the INT circuit is traced (cross-check T5); 0a1d keeps UnrealSpeccy's 28 T
constexpr ProfiFrame ProfiSyncPromFrame(ProfiSyncProm prom, MEM_MODEL model)
{
    switch (ProfiResolveSyncProm(prom, model))
    {
        case ProfiSyncProm::Samx6:    return {69888, 224, 12592, 32};
        case ProfiSyncProm::Fb0579b6: return {71680, 224, 48, 32};
        case ProfiSyncProm::V503:     return {69888, 224, 14368, 28};
        case ProfiSyncProm::Vr0a1d:
        case ProfiSyncProm::Default:
        default:                      return {69888, 224, 12580, 28};
    }
}

/// The frame T-state of the first paper dot in the Profi raster (line 72, T 24)
constexpr uint32_t kProfiPaperStartT = 72 * 224 + 24;

/// [ULA] intstart for a frame: INT fires at intstart + 1 (the Z80's strict compare), intToPaper T-states before
/// the paper
constexpr uint32_t ProfiIntStart(const ProfiFrame& f)
{
    return (kProfiPaperStartT + f.frame - f.intToPaper - 1) % f.frame;
}

/// [PROFI] SyncProm= value; false (and `out` untouched) for an unknown one. Empty / null = Default
bool ParseProfiSyncProm(const char* text, ProfiSyncProm& out);
const char* ProfiSyncPromName(ProfiSyncProm prom);
