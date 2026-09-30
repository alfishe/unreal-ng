#include "tsconftsu.h"

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

    /// 4 bpp pixel (x, y) of a 512x512 graphics bitmap at `page & 0xF8`; high nibble = left pixel
    inline uint8_t BitmapNibble(const uint8_t* ram, uint8_t page, uint32_t x, uint32_t y)
    {
        const uint8_t byte = ram[(static_cast<uint32_t>(page & 0xF8) << 14) + (y & 0x1FF) * 256 + ((x & 0x1FF) >> 1)];
        return (x & 1) ? (byte & 0x0F) : (byte >> 4);
    }
}

void TsConfTsu::Prefetch(const TsConfState& ts, const uint8_t* ram, uint32_t tmLine, MapRing& ring)
{
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
    }
}

void TsConfTsu::DrawTiles(const TsConfState& ts, const TsConfLine& set, const uint8_t* ram, const MapRing& ring,
                          uint32_t layer, uint32_t y, uint32_t width, uint8_t* out)
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

    for (uint32_t x = 0; x < width; x++)
    {
        const uint32_t tx = (x + xOffs) & 0x1FF;
        const uint16_t entry = ring[slot][(tx >> 3) & 0x3F][layer];
        const uint32_t tile = entry & 0x0FFF;
        if (tile == 0 && !drawZero)
            continue;

        const uint32_t fx = (entry & 0x4000) ? 7 - (tx & 7) : (tx & 7);
        const uint32_t fy = (entry & 0x8000) ? 7 - tileLine : tileLine;
        const uint8_t nibble = BitmapNibble(ram, gPage, (tile & 0x3F) * 8 + fx, (tile >> 6) * 8 + fy);
        if (nibble)
            out[x] = static_cast<uint8_t>(bank | (((entry >> 12) & 0x03) << 4) | nibble);
    }
}

void TsConfTsu::DrawSprites(const TsConfState& ts, const uint8_t* ram, uint32_t first, uint32_t end, uint32_t y,
                            uint32_t width, uint8_t* out)
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
        const bool xFlip = w1 & 0x8000;
        const uint32_t tile = w2 & 0x0FFF;
        const uint8_t pal = static_cast<uint8_t>((w2 >> 12) << 4);
        const uint32_t bitmapY = (tile >> 6) * 8 + fy;

        for (uint32_t fx = 0; fx < spriteWidth; fx++)
        {
            const uint32_t sx = ((w1 & 0x1FF) + fx) & 0x1FF;
            if (sx >= width)
                continue;
            const uint32_t bx = (tile & 0x3F) * 8 + (xFlip ? spriteWidth - 1 - fx : fx);
            const uint8_t nibble = BitmapNibble(ram, sgPage, bx, bitmapY);
            if (nibble)
                out[sx] = static_cast<uint8_t>(pal | nibble);
        }
    }
}

bool TsConfTsu::RenderLine(const TsConfState& ts, const TsConfLine& set, const uint8_t* ram, const MapRing& ring,
                           uint32_t y, uint32_t width, uint8_t* out)
{
    const uint8_t tConfig = ts.regs[TsConfReg::TConfig];
    if (!(tConfig & (kSpritesEnable | kTile0Enable | kTile1Enable)))
        return false;

    std::memset(out, 0, width);

    // Sprite layers: S0 runs to the first descriptor with LEAP, S1 to the
    // next one, S2 to descriptor 84; LEAP counts on inactive descriptors too
    uint32_t bounds[4] = {0, kDescriptors, kDescriptors, kDescriptors};
    uint32_t layer = 1;
    for (uint32_t d = 0; d < kDescriptors && layer < 3; d++)
    {
        if (ts.sfile[d * 3] & 0x4000)
            bounds[layer++] = d + 1;
    }

    const bool sprites = tConfig & kSpritesEnable;
    if (sprites)
        DrawSprites(ts, ram, bounds[0], bounds[1], y, width, out);
    if (tConfig & kTile0Enable)
        DrawTiles(ts, set, ram, ring, 0, y, width, out);
    if (sprites)
        DrawSprites(ts, ram, bounds[1], bounds[2], y, width, out);
    if (tConfig & kTile1Enable)
        DrawTiles(ts, set, ram, ring, 1, y, width, out);
    if (sprites)
        DrawSprites(ts, ram, bounds[2], bounds[3], y, width, out);
    return true;
}
