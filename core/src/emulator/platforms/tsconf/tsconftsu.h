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
/// v1 model: the line is rendered at once when it starts (the hardware
/// renders it during the previous line from ts_start), with no DRAM budget
/// (objects the hardware drops when starved are drawn - TSU-6/8, later).
class TsConfTsu
{
public:
    static constexpr uint32_t kMaxWidth = 360;
    static constexpr uint32_t kDescriptors = 85;

    /// Tilemap prefetch ring: [slot][column][layer] map words
    using MapRing = uint16_t[4][64][2];

    /// Prefetch the map words of `tmLine` (= TS line + 16, 9 bit) into the ring
    static void Prefetch(const TsConfState& ts, const uint8_t* ram, uint32_t tmLine, MapRing& ring);

    /// Render TS-window line `y` (0 = the window's first line) of width `width`
    /// @return true when anything was drawn (false: `out` left all transparent)
    static bool RenderLine(const TsConfState& ts, const TsConfLine& set, const uint8_t* ram, const MapRing& ring,
                           uint32_t y, uint32_t width, uint8_t* out);

private:
    static void DrawTiles(const TsConfState& ts, const TsConfLine& set, const uint8_t* ram, const MapRing& ring,
                          uint32_t layer, uint32_t y, uint32_t width, uint8_t* out);
    static void DrawSprites(const TsConfState& ts, const uint8_t* ram, uint32_t first, uint32_t end, uint32_t y,
                            uint32_t width, uint8_t* out);
};
