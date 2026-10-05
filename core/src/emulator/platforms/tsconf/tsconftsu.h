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
/// enabled layer for the prefetch, 2 per tile (8 pixels at 4 bpp), 2 per 8
/// pixels of a visible sprite line. A tile layer fetches width / 8 + 1 tiles
/// (33 / 41 / 46 for 256 / 320 / 360: video_ts.v:177 ends the layer at
/// tx == x_tiles, video_mode.v:186-189), also the one that falls past the
/// window when the X offset is a multiple of 8; tiles with number 0 and no
/// zero drawing cost nothing. The TSU gets what video and the CPU leave of the
/// pass; objects that no longer fit are dropped for that line, in the order
/// the TSU processes them (S0, T0, S1, T1, S2), as the hardware drops what it
/// could not render before the next ts_start.
///
/// When (TSU-6, TsConfEngine): the hardware draws line L during line L - 1
/// from ts_start (dot hpix_beg_ts - 1, [V] video_sync.v:130) to the next
/// ts_start, and the pass crosses line_start of L on a busy line. An object
/// takes T0/T1_G_PAGE, T0/T1_X_OFFS and PAL_SEL from the latch in force when
/// it is handed to the renderer (video_ts.v:162-171, video_ports.v:153-165):
/// L - 1's latch before line_start, L's after. In DRAM terms an object is late
/// when more than `split` TSU DRAM cycles of the pass came before it (the
/// cycles from ts_start to line_start that video and the CPU leave,
/// tools/machines/tsconf/rtl-sim tsulatch). The engine draws the objects up to
/// the split at ts_start (BeginLine) and the rest at line_start with L's latch
/// (FinishLine); T_CONFIG's layer enables are taken at ts_start, the pages,
/// the Y offsets and SFILE as they are when each part is drawn.
class TsConfTsu
{
public:
    static constexpr uint32_t kMaxWidth = 360;
    static constexpr uint32_t kDescriptors = 85;

    /// The sprite layers over SFILE: S0 is descriptors [bounds[0], bounds[1]), S1 [bounds[1], bounds[2]), S2
    /// [bounds[2], bounds[3]); bounds[3] is also where the sprites end - the THIRD LEAP ends them, or descriptor 84
    /// (a descriptor with LEAP belongs to the layer it ends; LEAP counts on inactive descriptors too)
    static void LayerBounds(const TsConfState& ts, uint32_t (&bounds)[4]);

    /// Tilemap prefetch ring: [slot][column][layer] map words
    using MapRing = uint16_t[4][64][2];

    /// Where a TSU pixel came from (the debug probe, ProbeLine)
    enum class Layer : uint8_t
    {
        None,
        S0,
        T0,
        S1,
        T1,
        S2,
    };
    struct Source
    {
        Layer layer = Layer::None;
        uint8_t descriptor = 0;      ///< sprites: SFILE descriptor 0..84
        uint8_t mapColumn = 0;       ///< tiles: tilemap column 0..63
        uint32_t mapAddress = 0;     ///< tiles: physical address of the map word (2 bytes)
        uint32_t graphicAddress = 0; ///< physical address of the graphics byte
        bool lowNibble = false;      ///< the pixel is the byte's low nibble
    };

    /// Prefetch the map words of `tmLine` (= TS line + 16, 9 bit) into the ring
    /// @return DRAM accesses used
    static uint32_t Prefetch(const TsConfState& ts, const uint8_t* ram, uint32_t tmLine, MapRing& ring);

    /// Render TS-window line `y` (0 = the window's first line) of width `width`
    /// within `budget` DRAM accesses (`used` returns what it took), all of it
    /// with one latched set
    /// @return true when the TSU is on (false: `out` untouched)
    static bool RenderLine(const TsConfState& ts, const TsConfLine& set, const uint8_t* ram, const MapRing& ring,
                           uint32_t y, uint32_t width, uint8_t* out, uint32_t budget, uint32_t& used);

    /// A pass split at line_start: where it stopped and what it needs to go on
    struct Pass
    {
        uint32_t y = 0;
        uint32_t width = 0;
        uint32_t budget = 0;
        uint32_t split = 0;    ///< objects after this many DRAM cycles of the pass are late
        uint32_t used = 0;     ///< DRAM cycles used so far (the prefetch included)
        uint8_t layers = 0;    ///< T_CONFIG layer enables at ts_start
        uint8_t phase = 0;     ///< next layer: 0 S0, 1 T0, 2 S1, 3 T1, 4 S2, 5 done
        uint8_t index = 0;     ///< next object of that layer: SFILE descriptor or tile
        bool paused = false;   ///< objects are left for FinishLine
    };

    /// Draw line `y` up to the split with `set` (the latch before line_start).
    /// `used` holds the prefetch's cycles. Objects past the split are left in
    /// `pass` (pass.paused) for FinishLine
    /// @return true when the TSU is on (false: `out` untouched)
    static bool BeginLine(const TsConfState& ts, const TsConfLine& set, const uint8_t* ram, const MapRing& ring,
                          uint32_t y, uint32_t width, uint8_t* out, uint32_t budget, uint32_t split, uint32_t used,
                          Pass& pass);
    /// Draw the objects BeginLine left, with `set` (the latch of line_start)
    static void FinishLine(const TsConfState& ts, const TsConfLine& set, const uint8_t* ram, const MapRing& ring,
                           uint8_t* out, Pass& pass);

    /// The whole pass, early objects with `set`, late ones with `lateSet`,
    /// noting each pixel's source (debug path; `sources` has `width` entries)
    static bool ProbeLine(const TsConfState& ts, const TsConfLine& set, const TsConfLine& lateSet, const uint8_t* ram,
                          const MapRing& ring, uint32_t y, uint32_t width, uint8_t* out, Source* sources,
                          uint32_t budget, uint32_t split, uint32_t& used);

private:
    enum class Result : uint8_t
    {
        Done,     ///< every object of the layer processed
        Starved,  ///< the budget ran out: the rest of the line is dropped
        Paused,   ///< stopped at the split (pause mode)
    };

    /// Process the pass from pass.phase / pass.index. Objects past pass.split
    /// take `lateSet`, or stop the pass when `pause`. kProbe: note sources
    /// (ProbeLine); the render path compiles without it
    template <bool kProbe>
    static void Run(const TsConfState& ts, const TsConfLine& set, const TsConfLine& lateSet, bool pause,
                    const uint8_t* ram, const MapRing& ring, uint8_t* out, Source* sources, Pass& pass);
    template <bool kProbe>
    static Result DrawTiles(const TsConfState& ts, const TsConfLine& set, const TsConfLine& lateSet, bool pause,
                            const uint8_t* ram, const MapRing& ring, uint32_t layer, uint8_t* out, Source* sources,
                            Pass& pass);
    template <bool kProbe>
    static Result DrawSprites(const TsConfState& ts, const uint8_t* ram, Layer layer, uint32_t first, uint32_t end,
                              bool pause, uint8_t* out, Source* sources, Pass& pass);
};
