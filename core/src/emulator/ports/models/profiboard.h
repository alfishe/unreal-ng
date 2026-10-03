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
#include <functional>

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

/// The hi-res (DS80) half of the same PROM (A10 = 80DS), in base 3.5 MHz T-states (research-profi-hires-timing.md,
/// tools/machines/profi/hires/ds80frames-output.txt). The generator runs from the 12 MHz crystal: one tick is 16
/// clocks, 1.333 us = 14/3 base T, and a line is 48 ticks, the same 64 us (224 base T) as in Spectrum mode. The CPU
/// clock changes instead (ProfiHiresClock). INT-to-paper and the INT length are rounded to whole base T (+-1/3 T;
/// the decode itself is +-1 tick). v5 INT: the generator ends it after 10 ticks or at the acknowledge
constexpr ProfiFrame ProfiSyncPromFrameHires(ProfiSyncProm prom, MEM_MODEL model)
{
    switch (ProfiResolveSyncProm(prom, model))
    {
        case ProfiSyncProm::Samx6:    return {69888, 224, 16147, 56};   // 312 lines; 3460 / 12 ticks
        case ProfiSyncProm::Fb0579b6: return {71680, 224, 3593, 47};    // 320 lines; 770 / 10 ticks
        case ProfiSyncProm::V503:     return {69888, 224, 12553, 47};   // 312 lines; 2690 / 10 ticks
        case ProfiSyncProm::Vr0a1d:
        case ProfiSyncProm::Default:
        default:                      return {71680, 224, 17939, 56};   // 320 lines; 3844 / 12 ticks
    }
}

/// The frame T-state of the first hi-res paper dot: 24 lines above the Spectrum paper (line 48, T 24), the raster
/// of ScreenProfi (M_PROFIHR in screen.h)
constexpr uint32_t kProfiHiresPaperStartT = 48 * 224 + 24;

/// [ULA] intstart for a hi-res frame (see ProfiIntStart)
constexpr uint32_t ProfiHiresIntStart(const ProfiFrame& f)
{
    return (kProfiHiresPaperStartT + f.frame - f.intToPaper - 1) % f.frame;
}

/// The CPU clock in hi-res as a fraction of 3.5 MHz, numerator over 7 (EmulatorState::hw_turbo_ratio / hw_clock_den):
/// v3 12 MHz / 4 = 3 MHz = 6/7 (turbo 12/7); v5 ZQ3 / 4 (ZQ3 the third crystal, [PROFI] ZQ3MHz, 16-24, even) =
/// ZQ3/2 sevenths, turbo ZQ3 sevenths: 20 MHz gives 5 MHz = 10/7 (research-profi-hires-timing.md, Emulator rules)
constexpr uint8_t kProfiHiresClockDen = 7;
constexpr uint8_t ProfiHiresClockNum(bool v5, uint8_t zq3MHz, bool turbo)
{
    const uint8_t num = v5 ? static_cast<uint8_t>(zq3MHz / 2) : 6;
    return turbo ? static_cast<uint8_t>(num * 2) : num;
}

/// TTD time units per base T for a board: the least common multiple of the numerators it can select (1, 2 in
/// Spectrum mode; the hi-res pair) - EmulatorState::ttd_clock_units
constexpr uint8_t ProfiTtdClockUnits(bool v5, uint8_t zq3MHz)
{
    // n and 2n with n even (6, 8, 10, 12) or odd (9, 11): 2n covers 1, 2, n and 2n
    return static_cast<uint8_t>(ProfiHiresClockNum(v5, zq3MHz, true));
}

/// Hi-res video timing in ns from the frame start (design-hires.md phase H3, research-profi-hires-timing.md 3-4). The
/// generator counts 12 MHz clocks: a tick is 16 of them (1333.3 ns), a video request every half tick (666.7 ns). The
/// fetch window of paper line L is 32 ticks and leads the displayed dots by one tick, as in Spectrum mode
constexpr double kProfiBaseTNs = 2000.0 / 7.0;            ///< one base T (3.5 MHz)
constexpr double kProfiLineNs = 64000.0;                  ///< 224 base T
constexpr double kProfiHiresTickNs = 4000.0 / 3.0;
constexpr double kProfiHiresRequestNs = kProfiHiresTickNs / 2.0;
constexpr uint32_t kProfiHiresTicksPerWindow = 32;
constexpr uint32_t kProfiHiresPaperLines = 240;
/// The start of paper line `line`'s fetch window
constexpr double ProfiHiresWindowStartNs(uint32_t line)
{
    return (static_cast<double>(kProfiHiresPaperStartT) + 224.0 * line) * kProfiBaseTNs - kProfiHiresTickNs;
}
/// CPU clock period in ns for a hi-res clock numerator (num / 7 of 3.5 MHz = num / 2 MHz)
constexpr double ProfiHiresCpuPeriodNs(uint32_t num) { return num ? 2000.0 / num : 0.0; }

/// [PROFI] ZQ3MHz: the v5's third crystal, 16-24 MHz (the 5.0 album's table), even values only (the clock is half of
/// it in sevenths); default 20 (the 5.06 parts list: C12 = 27 pF is the 20 MHz entry)
constexpr uint8_t kProfiZq3DefaultMHz = 20;
constexpr uint8_t ProfiClampZq3(long mhz)
{
    if (mhz < 16 || mhz > 24)
        return kProfiZq3DefaultMHz;
    return static_cast<uint8_t>(mhz & ~1L);
}

/// [PROFI] SyncProm= value; false (and `out` untouched) for an unknown one. Empty / null = Default
bool ParseProfiSyncProm(const char* text, ProfiSyncProm& out);
const char* ProfiSyncPromName(ProfiSyncProm prom);

/// The keyboard on the Profi's keyboard connector ([PROFI] Keyboard=; design section "Keyboard",
/// research-profi-keyboard.md). Default is the board's usual one: the PROFI-XT controller on v5, the
/// mechanical matrix keyboard on v3
enum class ProfiKeyboard : uint8_t
{
    Default = 0,
    Matrix,     ///< "matrix": the 40-key Spectrum matrix (host keys through the ZX matrix, Shift = Caps Shift)
    Xt,         ///< "xt": the PROFI-XT controller running its firmware on the MCS-48 core (ProfiXtKbc)
    XtTable,    ///< "xttable": the PROFI-XT controller from its key table, no MCU (ProfiXtKbc table engine)
};

/// The keyboard `keyboard` resolves to on `model` (Default: v5 XT, v3 Matrix)
constexpr ProfiKeyboard ProfiResolveKeyboard(ProfiKeyboard keyboard, MEM_MODEL model)
{
    if (keyboard != ProfiKeyboard::Default)
        return keyboard;
    return model == MM_PROFI ? ProfiKeyboard::Xt : ProfiKeyboard::Matrix;
}

/// [PROFI] Keyboard= value (matrix | xt | xttable | default, case-insensitive); false (and `out` untouched) for an
/// unknown one. Empty / null = Default
bool ParseProfiKeyboard(const char* text, ProfiKeyboard& out);
/// The config name: "default", "matrix", "xt", "xttable"
const char* ProfiKeyboardName(ProfiKeyboard keyboard);
/// The create-time config override for a keyboard choice (every automation surface: WebAPI "profi": {"keyboard"},
/// CLI --profi-keyboard, MCP profi_keyboard, Lua / Python profi_keyboard); Default = no override
std::function<void(CONFIG&)> ProfiKeyboardOverride(ProfiKeyboard keyboard);

/// Create-time override of the hi-res clocks: `zq3MHz` 0 = keep [PROFI] ZQ3MHz (else 16-24, even); `ayClockNew` -1 =
/// keep [PROFI] AyClock, 0 = old (1.5 MHz in hi-res), 1 = new (1.75 MHz always). Empty when nothing changes
std::function<void(CONFIG&)> ProfiClockOverride(uint8_t zq3MHz, int ayClockNew);

/// The CPU clock in hi-res for a board (Hz, no turbo): v3 3 MHz, v5 ZQ3 / 4
constexpr uint32_t ProfiHiresCpuHz(bool v5, uint8_t zq3MHz)
{
    return static_cast<uint32_t>(ProfiHiresClockNum(v5, ProfiClampZq3(zq3MHz), false)) * 500000u;
}

class EmulatorContext;
/// The keyboard a running Profi has fitted (after a fallback: no firmware image -> XtTable); Default when the
/// machine is no Profi. The one source every automation surface reports (paging.profi_keyboard)
ProfiKeyboard ProfiKeyboardInForce(const EmulatorContext* context);

