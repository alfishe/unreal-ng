#pragma once

#include <cstdint>

struct TsConfState;
struct TsConfLine;

/// TS-Conf TSU: two tile layers and three sprite layers (hardware-spec §4.4).
///
/// Renders one line of the TS window into a line buffer of 8-bit CRAM
/// indices, 0 = transparent (an opaque pixel always has a non-zero low
/// nibble). Layer order bottom to top S0, T0, S1, T1, S2; a pixel with color
/// nibble 0 is not written.
///
/// Registers: T_CONFIG, T_MAP_PAGE, SG_PAGE, T0/T1_Y_OFFS and SFILE are read
/// as they are when the line is rendered; T0/T1_G_PAGE, T0/T1_X_OFFS and
/// PAL_SEL come from the line-latched set.
///
/// Tile map entries come from the prefetch ring, as on the hardware
/// ([V] video_ts.v:121-139): on every TS line the TSU fetches 8 map words per
/// enabled layer for TS line + 16, from map row (line + 16) / 8 + the coarse
/// Y offset of that moment, into ring slot ((line + 16) / 8) % 4; a full 64-
/// entry row takes 8 lines. Rendering reads slot ((y + fine Y) / 8) % 4, so
/// the coarse Y bits act about 16 lines late and the fine bits at once (TSU-7).
///
/// DRAM budget (TSU-8): every fetch costs DRAM accesses - 8 map words per
/// enabled layer for the prefetch, 2 per drawn tile (8 pixels at 4 bpp), 2
/// per 8 pixels of a visible sprite line. The TSU gets what video and the CPU
/// leave of the line's 448 accesses; objects that no longer fit are dropped
/// for that line, in the order the TSU processes them (S0, T0, S1, T1, S2),
/// as the hardware drops what it could not render before the next ts_start.
///
/// v1 model: the line is rendered at once when it starts (the hardware
/// renders it during the previous line from ts_start - TSU-6).
class TsConfTsu
{
public:
    static constexpr uint32_t kMaxWidth = 360;
    static constexpr uint32_t kDescriptors = 85;

    /// Tilemap prefetch ring: [slot][column][layer] map words
    using MapRing = uint16_t[4][64][2];

    /// Prefetch the map words of `tmLine` (= TS line + 16, 9 bit) into the ring
    /// @return DRAM accesses used
    static uint32_t Prefetch(const TsConfState& ts, const uint8_t* ram, uint32_t tmLine, MapRing& ring);

    /// Render TS-window line `y` (0 = the window's first line) of width `width`
    /// within `budget` DRAM accesses (`used` returns what it took)
    /// @return true when the TSU is on (false: `out` untouched)
    static bool RenderLine(const TsConfState& ts, const TsConfLine& set, const uint8_t* ram, const MapRing& ring,
                           uint32_t y, uint32_t width, uint8_t* out, uint32_t budget, uint32_t& used);

private:
    /// @return false when the budget ran out (the rest of the line is dropped)
    static bool DrawTiles(const TsConfState& ts, const TsConfLine& set, const uint8_t* ram, const MapRing& ring,
                          uint32_t layer, uint32_t y, uint32_t width, uint8_t* out, uint32_t budget, uint32_t& used);
    static bool DrawSprites(const TsConfState& ts, const uint8_t* ram, uint32_t first, uint32_t end, uint32_t y,
                            uint32_t width, uint8_t* out, uint32_t budget, uint32_t& used);
};
