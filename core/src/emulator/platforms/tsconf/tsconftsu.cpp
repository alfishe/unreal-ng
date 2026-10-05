#include "tsconftsu.h"

#include <algorithm>
#include <cstring>

#include "emulator/platforms/tsconf/tsconfengine.h"
#include "emulator/platforms/tsconf/tsconfstate.h"

namespace
{
    constexpr uint8_t kSpritesEnable = 0x80;  // T_CONFIG[7]
    constexpr uint8_t kTile1Enable = 0x40;    // T_CONFIG[6]
    constexpr uint8_t kTile0Enable = 0x20;    // T_CONFIG[5]
    constexpr uint8_t kTile1Zero = 0x08;      // T_CONFIG[3]: draw tile number 0
    constexpr uint8_t kTile0Zero = 0x04;      // T_CONFIG[2]

    /// Physical address of the byte holding 4 bpp pixel (x, y) of a 512x512 bitmap at `page & 0xF8`
    inline uint32_t BitmapAddress(uint8_t page, uint32_t x, uint32_t y)
    {
        return (static_cast<uint32_t>(page & 0xF8) << 14) + (y & 0x1FF) * 256 + ((x & 0x1FF) >> 1);
    }

    /// 4 bpp pixel (x, y) of a 512x512 graphics bitmap at `page & 0xF8`; high nibble = left pixel
    inline uint8_t BitmapNibble(const uint8_t* ram, uint8_t page, uint32_t x, uint32_t y)
    {
        const uint8_t byte = ram[BitmapAddress(page, x, y)];
        return (x & 1) ? (byte & 0x0F) : (byte >> 4);
    }
}

uint32_t TsConfTsu::Prefetch(const TsConfState& ts, const uint8_t* ram, uint32_t tmLine, MapRing& ring)
{
    uint32_t used = 0;
    const uint8_t* r = ts.regs;
    const uint8_t tConfig = r[TsConfReg::TConfig];
    const uint8_t* map = ram + (static_cast<uint32_t>(r[TsConfReg::TMapPage]) << 14);
    const uint32_t slot = (tmLine >> 3) & 0x03;
    const uint32_t burst = tmLine & 0x07;

    for (uint32_t layer = 0; layer < 2; layer++)
    {
        if (!(tConfig & (layer ? kTile1Enable : kTile0Enable)))
            continue;
        const uint32_t yRegister = TsConfReg::T0XOffsL + (layer ? 6u : 2u);
        const uint32_t coarse = (r[yRegister] | ((r[yRegister + 1] & 1u) << 8)) >> 3;
        const uint8_t* row = map + (((tmLine >> 3) + coarse) & 0x3F) * 256 + layer * 128;
        for (uint32_t n = 0; n < 8; n++)
        {
            const uint32_t column = burst * 8 + n;
            ring[slot][column][layer] = static_cast<uint16_t>(row[column * 2] | (row[column * 2 + 1] << 8));
        }
        used += 8;
    }
    return used;
}

template <bool kProbe>
bool TsConfTsu::DrawTiles(const TsConfState& ts, const TsConfLine& set, const uint8_t* ram, const MapRing& ring,
                          uint32_t layer, uint32_t y, uint32_t width, uint8_t* out, [[maybe_unused]] Source* sources,
                          uint32_t budget, uint32_t& used)
{
    const uint8_t* r = ts.regs;
    const uint8_t tConfig = r[TsConfReg::TConfig];
    const bool drawZero = tConfig & (layer ? kTile1Zero : kTile0Zero);
    const uint32_t yRegister = TsConfReg::T0XOffsL + (layer ? 6u : 2u);
    const uint32_t fine = r[yRegister] & 0x07;
    const uint32_t xOffs = layer ? set.t1XOffs : set.t0XOffs;
    const uint8_t gPage = layer ? set.t1GPage : set.t0GPage;
    const uint8_t bank = static_cast<uint8_t>(((set.palSel >> (layer ? 6 : 4)) & 0x03) << 6);

    // The ring slot and the tile line: (y + fine Y) of a 5-bit line counter
    const uint32_t tLine = ((y & 0x1F) + fine) & 0x1F;
    const uint32_t slot = tLine >> 3;
    const uint32_t tileLine = tLine & 0x07;

    // Tiles left to right; the first starts at -(X offset & 7)
    for (int32_t x0 = -static_cast<int32_t>(xOffs & 7), k = 0; x0 < static_cast<int32_t>(width); x0 += 8, k++)
    {
        const uint32_t column = ((xOffs >> 3) + static_cast<uint32_t>(k)) & 0x3F;
        const uint16_t entry = ring[slot][column][layer];
        const uint32_t tile = entry & 0x0FFF;
        if (tile == 0 && !drawZero)
            continue;  // skipped: no graphics fetch
        // Two words; a cut tile shows the 4 pixels of its first word ([V] video_ts_render.v:84-107)
        const uint32_t words = std::min<uint32_t>(2, budget > used ? budget - used : 0);
        if (!words)
            return false;
        used += words;
        const uint32_t fetched = words * 4;

        const uint32_t fy = (entry & 0x8000) ? 7 - tileLine : tileLine;
        const uint8_t index = static_cast<uint8_t>(bank | (((entry >> 12) & 0x03) << 4));
        for (int32_t px = 0; px < 8; px++)
        {
            const int32_t x = x0 + px;
            if (x < 0 || x >= static_cast<int32_t>(width))
                continue;
            const uint32_t fx = (entry & 0x4000) ? 7 - static_cast<uint32_t>(px) : static_cast<uint32_t>(px);
            if (fx >= fetched)
                continue;  // its word was not fetched
            const uint32_t bx = (tile & 0x3F) * 8 + fx;
            const uint32_t by = (tile >> 6) * 8 + fy;
            const uint8_t nibble = BitmapNibble(ram, gPage, bx, by);
            if (nibble)
            {
                out[x] = static_cast<uint8_t>(index | nibble);
                if constexpr (kProbe)
                {
                    // The map word the ring slot holds: the row it was prefetched from
                    const uint32_t yRegisterNow = TsConfReg::T0XOffsL + (layer ? 6u : 2u);
                    const uint32_t mapRow = ((((y & 0x1FF) + (r[yRegisterNow] | ((r[yRegisterNow + 1] & 1u) << 8))) >> 3)) & 0x3F;
                    Source& src = sources[x];
                    src.layer = layer ? Layer::T1 : Layer::T0;
                    src.mapColumn = static_cast<uint8_t>(column);
                    src.mapAddress = (static_cast<uint32_t>(r[TsConfReg::TMapPage]) << 14) + mapRow * 256 + layer * 128 + column * 2;
                    src.graphicAddress = BitmapAddress(gPage, bx, by);
                    src.lowNibble = bx & 1;
                }
            }
        }
        if (fetched < 8)
            return false;  // the budget ran out inside this tile
    }
    return true;
}

template <bool kProbe>
bool TsConfTsu::DrawSprites(const TsConfState& ts, const uint8_t* ram, [[maybe_unused]] Layer layer, uint32_t first,
                            uint32_t end, uint32_t y, uint32_t width, uint8_t* out, [[maybe_unused]] Source* sources,
                            uint32_t budget, uint32_t& used)
{
    const uint8_t sgPage = ts.regs[TsConfReg::SGPage];
    for (uint32_t d = first; d < end; d++)
    {
        const uint16_t w0 = ts.sfile[d * 3];
        const uint16_t w1 = ts.sfile[d * 3 + 1];
        const uint16_t w2 = ts.sfile[d * 3 + 2];
        if (!(w0 & 0x2000))
            continue;  // not active

        const uint32_t yMax = ((w0 >> 9) & 0x07) * 8 + 7;
        const uint32_t line = (y - (w0 & 0x1FF)) & 0x1FF;
        if (line > yMax)
            continue;

        const uint32_t fy = (w0 & 0x8000) ? yMax - line : line;
        const uint32_t spriteWidth = (((w1 >> 9) & 0x07) + 1) * 8;
        const uint32_t cost = spriteWidth / 4;  // 4 bpp: 4 pixels per word
        // A cut sprite shows the pixels of the words it got, in bitmap order ([V] video_ts_render.v:84-107)
        const uint32_t words = std::min(cost, budget > used ? budget - used : 0u);
        if (!words)
            return false;
        used += words;
        const uint32_t fetched = words * 4;

        const bool xFlip = w1 & 0x8000;
        const uint32_t tile = w2 & 0x0FFF;
        const uint8_t pal = static_cast<uint8_t>((w2 >> 12) << 4);
        const uint32_t bitmapY = (tile >> 6) * 8 + fy;

        for (uint32_t fx = 0; fx < spriteWidth; fx++)
        {
            const uint32_t sx = ((w1 & 0x1FF) + fx) & 0x1FF;
            const uint32_t bitmapX = xFlip ? spriteWidth - 1 - fx : fx;
            if (sx >= width || bitmapX >= fetched)
                continue;
            const uint32_t bx = (tile & 0x3F) * 8 + bitmapX;
            const uint8_t nibble = BitmapNibble(ram, sgPage, bx, bitmapY);
            if (nibble)
            {
                out[sx] = static_cast<uint8_t>(pal | nibble);
                if constexpr (kProbe)
                {
                    Source& src = sources[sx];
                    src.layer = layer;
                    src.descriptor = static_cast<uint8_t>(d);
                    src.mapColumn = 0;
                    src.mapAddress = 0;
                    src.graphicAddress = BitmapAddress(sgPage, bx, bitmapY);
                    src.lowNibble = bx & 1;
                }
            }
        }
        if (words < cost)
            return false;  // the budget ran out inside this sprite
    }
    return true;
}

void TsConfTsu::LayerBounds(const TsConfState& ts, uint32_t (&bounds)[4])
{
    // S0 runs to the first descriptor with LEAP, S1 to the next one, S2 to the third LEAP or to descriptor 84. LEAP
    // counts on inactive descriptors too, and a descriptor with LEAP belongs to the layer it ends. Nothing behind the
    // third LEAP is processed: the layer machine has ended the last layer ([V] video_ts.v:263-274, layer_skip |=
    // layer_end), which is how a program ends its list while the rest of the SFILE holds anything (zifi.spg loads all
    // 256 words from a table followed by text)
    bounds[0] = 0;
    bounds[1] = bounds[2] = bounds[3] = kDescriptors;
    uint32_t layer = 1;
    for (uint32_t d = 0; d < kDescriptors && layer < 4; d++)
    {
        if (ts.sfile[d * 3] & 0x4000)
            bounds[layer++] = d + 1;
    }
}

template <bool kProbe>
bool TsConfTsu::Render(const TsConfState& ts, const TsConfLine& set, const uint8_t* ram, const MapRing& ring,
                       uint32_t y, uint32_t width, uint8_t* out, Source* sources, uint32_t budget, uint32_t& used)
{
    const uint8_t tConfig = ts.regs[TsConfReg::TConfig];
    if (!(tConfig & (kSpritesEnable | kTile0Enable | kTile1Enable)))
        return false;

    std::memset(out, 0, width);
    if constexpr (kProbe)
    {
        for (uint32_t x = 0; x < width; x++)
            sources[x] = Source{};
    }

    uint32_t bounds[4];
    LayerBounds(ts, bounds);

    // Processing order S0, T0, S1, T1, S2 is also the drawing order; the
    // first object that does not fit ends the line
    const bool sprites = tConfig & kSpritesEnable;
    if (sprites && !DrawSprites<kProbe>(ts, ram, Layer::S0, bounds[0], bounds[1], y, width, out, sources, budget, used))
        return true;
    if ((tConfig & kTile0Enable) && !DrawTiles<kProbe>(ts, set, ram, ring, 0, y, width, out, sources, budget, used))
        return true;
    if (sprites && !DrawSprites<kProbe>(ts, ram, Layer::S1, bounds[1], bounds[2], y, width, out, sources, budget, used))
        return true;
    if ((tConfig & kTile1Enable) && !DrawTiles<kProbe>(ts, set, ram, ring, 1, y, width, out, sources, budget, used))
        return true;
    if (sprites)
        DrawSprites<kProbe>(ts, ram, Layer::S2, bounds[2], bounds[3], y, width, out, sources, budget, used);
    return true;
}

bool TsConfTsu::RenderLine(const TsConfState& ts, const TsConfLine& set, const uint8_t* ram, const MapRing& ring,
                           uint32_t y, uint32_t width, uint8_t* out, uint32_t budget, uint32_t& used)
{
    return Render<false>(ts, set, ram, ring, y, width, out, nullptr, budget, used);
}

bool TsConfTsu::ProbeLine(const TsConfState& ts, const TsConfLine& set, const uint8_t* ram, const MapRing& ring,
                          uint32_t y, uint32_t width, uint8_t* out, Source* sources, uint32_t budget, uint32_t& used)
{
    return Render<true>(ts, set, ram, ring, y, width, out, sources, budget, used);
}
