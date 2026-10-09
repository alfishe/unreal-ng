#include "stdafx.h"

#include "nextvideorenderer.h"

#include <cstring>

namespace
{
constexpr unsigned kW = NextVideoRenderer::kWidth;

uint16_t Nine(uint8_t rrrgggbb)
{
    return static_cast<uint16_t>((rrrgggbb << 1) | ((rrrgggbb & 3) ? 1 : 0));
}

uint8_t Expand3(unsigned v)
{
    return static_cast<uint8_t>((v << 5) | (v << 2) | (v >> 1));
}
}  // namespace

uint32_t NextVideoRenderer::Rgba(uint16_t colour9)
{
    const unsigned r = (colour9 >> 6) & 7;
    const unsigned g = (colour9 >> 3) & 7;
    const unsigned b = colour9 & 7;
    return 0xFF000000u | (static_cast<uint32_t>(Expand3(b)) << 16) | (static_cast<uint32_t>(Expand3(g)) << 8) | Expand3(r);
}

/// The ULA layer of line y: the border colour around the 256 x 192 paper, the paper in the Timex mode of port #FF
void NextVideoRenderer::UlaLine(const NextVideoInputs& in, unsigned y, Pixel* line)
{
    const NextVideoRegs& regs = *in.regs;
    if (in.nr[0x68] & 0x80)
        return;  // ULA output disabled: transparent
    const unsigned palette = (regs.PaletteControl() & 0x02) ? 4 : 0;  // NR #43 bit 1: the second ULA palette
    const bool ulaNext = (regs.PaletteControl() & 0x01) != 0;
    const uint8_t mask = regs.UlaNextFormat();
    const bool fullInk = ulaNext && mask == 0xFF;

    auto border = [&](Pixel& p) {
        if (fullInk)
            return;  // paper and border come from the fallback colour: transparent here
        p.colour = regs.PaletteEntry(palette, (ulaNext ? 128 : 16) + in.border) & 0x1FF;
        p.opaque = true;
        p.border = true;
    };
    const bool inPaperRows = y >= kPaperTop && y < kPaperTop + 192;
    if (!inPaperRows)
    {
        for (unsigned x = 0; x < kW; x++)
            border(line[x]);
        return;
    }
    for (unsigned x = 0; x < kPaperLeft; x++)
        border(line[x]);
    for (unsigned x = kPaperLeft + 512; x < kW; x++)
        border(line[x]);

    const unsigned py = y - kPaperTop;
    const unsigned sy = (py + in.nr[0x27]) % 192;
    const unsigned scrollX = in.nr[0x26];
    const unsigned base = in.shadowScreen ? 7 : 5;
    const uint8_t* screen = in.ram + static_cast<size_t>(base) * 0x4000;
    const unsigned bitmapRow = ((sy & 0xC0) << 5) | ((sy & 7) << 8) | ((sy & 0x38) << 2);
    const unsigned attrRow = 0x1800 + (sy >> 3) * 32;
    const unsigned timexMode = in.portFf & 7;

    auto index = [&](bool ink, uint8_t attr) -> int {
        unsigned bright = (attr >> 6) & 1;
        bool inkPixel = ink;
        if ((attr & 0x80) && in.flash)
            inkPixel = !inkPixel;
        if (ulaNext)
        {
            unsigned bits = 0;
            for (unsigned m = mask; m & 1; m >>= 1)
                bits++;
            if (fullInk)
                return inkPixel ? attr : -1;
            const bool solidMask = mask && ((mask + 1) & mask) == 0;
            if (!solidMask)
                return inkPixel ? (attr & mask) : -1;
            return inkPixel ? (attr & mask) : 128 + (attr >> bits);
        }
        return inkPixel ? static_cast<int>((attr & 7) + 8 * bright) : static_cast<int>(16 + ((attr >> 3) & 7) + 8 * bright);
    };
    auto put = [&](unsigned sub, int idx) {
        Pixel& p = line[kPaperLeft + sub];
        if (idx < 0)
        {
            p.opaque = false;
            return;
        }
        p.colour = regs.PaletteEntry(palette, static_cast<unsigned>(idx)) & 0x1FF;
        p.opaque = true;
    };

    if (timexMode == 6)
    {
        // hi-res 512 x 192: the even bytes from screen 0, the odd from screen 1; ink = bits 5:3, paper the opposite
        const unsigned ink = (in.portFf >> 3) & 7;
        for (unsigned xb = 0; xb < 32; xb++)
            for (unsigned half = 0; half < 2; half++)
            {
                const uint8_t byte = screen[half * 0x2000 + bitmapRow + ((xb + (scrollX >> 3)) & 31)];
                for (unsigned bit = 0; bit < 8; bit++)
                    put((xb * 2 + half) * 8 + bit, ((byte >> (7 - bit)) & 1) ? static_cast<int>(ink) : static_cast<int>(16 + (7 - ink)));
            }
        return;
    }
    const unsigned bitmapBase = (timexMode & 1) ? 0x2000 : 0;  // screen 1 is the second half (modes 0 / 1 only)
    for (unsigned px = 0; px < 256; px++)
    {
        const unsigned sx = (px + scrollX) & 255;
        const unsigned col = sx >> 3;
        const uint8_t byte = screen[(timexMode == 2 ? 0 : bitmapBase) + bitmapRow + col];
        const uint8_t attr = timexMode == 2 ? screen[0x2000 + bitmapRow + col]  // hi-colour: an attribute per 8 x 1
                                            : screen[bitmapBase + attrRow + col];
        const int idx = index(((byte >> (7 - (sx & 7))) & 1) != 0, attr);
        put(px * 2, idx);
        put(px * 2 + 1, idx);
    }
}

/// LoRes (NR #15 bit 7): 128 x 96 at 256 colours of the ULA palette, in the two halves of the ULA screen
void NextVideoRenderer::LoResLine(const NextVideoInputs& in, unsigned y, Pixel* line)
{
    const NextVideoRegs& regs = *in.regs;
    if (in.nr[0x68] & 0x80 || y < kPaperTop || y >= kPaperTop + 192)
        return;
    const unsigned palette = (regs.PaletteControl() & 0x02) ? 4 : 0;
    const unsigned offset = (in.nr[0x6A] & 0x0F) << 4;
    const unsigned py = y - kPaperTop;
    const unsigned sy = (py + in.nr[0x33]) % 192;  // NR #33: LoRes Y scroll
    const unsigned row = sy / 2;                       // 96 rows
    const unsigned base = in.shadowScreen ? 7 : 5;
    const uint8_t* screen = in.ram + static_cast<size_t>(base) * 0x4000;
    const unsigned scrollX = in.nr[0x32];  // NR #32: LoRes X scroll, in LoRes pixels doubled
    for (unsigned px = 0; px < 256; px++)
    {
        const unsigned sx = ((px + scrollX) & 255) / 2;  // 128 columns
        const unsigned address = (row < 48 ? 0x0000 : 0x2000) + (row % 48) * 128 + sx;
        const unsigned idx = (screen[address] + offset) & 0xFF;
        Pixel& a = line[kPaperLeft + px * 2];
        a.colour = regs.PaletteEntry(palette, idx) & 0x1FF;
        a.opaque = true;
        line[kPaperLeft + px * 2 + 1] = a;
    }
}

/// Layer 2: 256 x 192 x 8 (row-major), 320 x 256 x 8 and 640 x 256 x 4 (column-major), from NR #12's bank
void NextVideoRenderer::Layer2Line(const NextVideoInputs& in, unsigned y, Pixel* line)
{
    if (!in.layer2Enable)
        return;
    const NextVideoRegs& regs = *in.regs;
    const unsigned resolution = (in.nr[0x70] >> 4) & 3;
    const unsigned palette = (regs.PaletteControl() & 0x04) ? 5 : 1;  // NR #43 bit 2: the second Layer 2 palette
    const unsigned offset = (in.nr[0x70] & 0x0F) << 4;
    const unsigned bank = in.nr[0x12] & 0x7F;
    const unsigned scrollX = in.nr[0x16] | ((in.nr[0x71] & 1) << 8);
    const unsigned scrollY = in.nr[0x17];
    const uint8_t* base = in.ram + static_cast<size_t>(bank) * 0x4000;
    const size_t limit = static_cast<size_t>(in.ramPages) * 0x4000;
    if (static_cast<size_t>(bank) * 0x4000 >= limit)
        return;
    const unsigned x1 = regs.Clip(0, 0), x2 = regs.Clip(0, 1), y1 = regs.Clip(0, 2), y2 = regs.Clip(0, 3);
    auto available = [&](size_t address) { return static_cast<size_t>(bank) * 0x4000 + address < limit; };

    if (resolution == 0)
    {
        if (y < kPaperTop || y >= kPaperTop + 192)
            return;
        const unsigned py = y - kPaperTop;
        if (py < y1 || py > y2)
            return;
        const unsigned sy = (py + scrollY) % 192;
        for (unsigned px = x1; px <= x2 && px < 256; px++)
        {
            const unsigned sx = (px + scrollX) & 255;
            const size_t address = static_cast<size_t>(sy) * 256 + sx;
            if (!available(address))
                continue;
            const unsigned idx = (base[address] + offset) & 0xFF;
            const uint16_t colour = regs.PaletteEntry(palette, idx);
            for (unsigned s = 0; s < 2; s++)
            {
                Pixel& p = line[kPaperLeft + px * 2 + s];
                p.colour = colour;
                p.opaque = true;
            }
        }
        return;
    }
    // the whole 320 x 256 grid; the clip's X units are two pixels, its Y the line
    if (y < y1 || y > y2)
        return;
    const unsigned sy = (y + scrollY) & 255;
    if (resolution == 1)
    {
        for (unsigned px = x1 * 2; px <= x2 * 2 + 1 && px < 320; px++)
        {
            const unsigned sx = (px + scrollX) % 320;
            const size_t address = static_cast<size_t>(sx) * 256 + sy;
            if (!available(address))
                continue;
            const unsigned idx = (base[address] + offset) & 0xFF;
            const uint16_t colour = regs.PaletteEntry(palette, idx);
            for (unsigned s = 0; s < 2; s++)
            {
                Pixel& p = line[px * 2 + s];
                p.colour = colour;
                p.opaque = true;
            }
        }
        return;
    }
    // 640 x 256 x 4: two nibbles per byte, the high nibble on the left
    for (unsigned px = x1 * 4; px <= x2 * 4 + 3 && px < 640; px++)
    {
        const unsigned sx = (px + scrollX * 2) % 640;
        const size_t address = static_cast<size_t>(sx / 2) * 256 + sy;
        if (!available(address))
            continue;
        const unsigned nibble = (sx & 1) ? (base[address] & 0x0F) : (base[address] >> 4);
        const unsigned idx = ((offset & 0xF0) | nibble) & 0xFF;
        Pixel& p = line[px];
        p.colour = regs.PaletteEntry(palette, idx);
        p.opaque = true;
    }
}

/// The tilemap (NR #6B-#6F, #2F-#31, #4C): 40 or 80 columns of 8 x 8 tiles of 4-bit pixels (or 1-bit text tiles) read
/// from bank 5 / 7 at 256-byte offsets, scrolled and wrapped, with mirror / rotate / palette offset per tile
void NextVideoRenderer::TilemapLine(const NextVideoInputs& in, unsigned y, Pixel* line)
{
    const uint8_t control = in.nr[0x6B];
    if (!(control & 0x80))
        return;
    const NextVideoRegs& regs = *in.regs;
    if (y < regs.Clip(3, 2) || y > regs.Clip(3, 3))
        return;
    const bool wide = (control & 0x40) != 0;  // 80 columns
    const bool noFlags = (control & 0x20) != 0;
    const bool text = (control & 0x08) != 0;
    const bool mode512 = (control & 0x02) != 0;
    const bool ulaOnTop = (control & 0x01) != 0;
    const unsigned palette = (control & 0x10) ? 7 : 3;
    const unsigned transparentIndex = in.nr[0x4C] & 0x0F;
    const unsigned scrollX = in.nr[0x30] | ((in.nr[0x2F] & 3) << 8);
    const unsigned scrollY = in.nr[0x31];
    auto vram = [&](uint8_t base, unsigned offset) -> uint8_t {
        const unsigned page = (base & 0x80) ? 7 : 5;
        const size_t address = static_cast<size_t>(page) * 0x4000 + ((static_cast<size_t>(base & 0x3F) * 256 + offset) & 0x3FFF);
        return address < static_cast<size_t>(in.ramPages) * 0x4000 ? in.ram[address] : 0;
    };
    const unsigned columns = wide ? 80 : 40;
    const unsigned wrap = wide ? 640 : 320;
    const unsigned absY = (y + scrollY) & 0xFF;
    const unsigned tileRow = absY >> 3, pixelY = absY & 7;
    const unsigned x1 = regs.Clip(3, 0) * 2, x2 = regs.Clip(3, 1) * 2 + 1;
    int cachedColumn = -1;
    unsigned paletteOffset = 0;
    bool rotate = false, xMirror = false, yMirror = false, below = false;
    unsigned tile = 0;
    uint8_t textRow = 0, row[4] = {};
    for (unsigned sx = 0; sx < kW; sx++)
    {
        const unsigned px320 = sx >> 1;
        if (px320 < x1 || px320 > x2)
            continue;
        const unsigned tx = wide ? sx : px320;
        const unsigned absX = (tx + scrollX) % wrap;
        const unsigned column = absX >> 3, pixelX = absX & 7;
        if (static_cast<int>(column) != cachedColumn)
        {
            cachedColumn = static_cast<int>(column);
            const unsigned entry = (tileRow * columns + column) * (noFlags ? 1 : 2);
            tile = vram(in.nr[0x6E], entry);
            const uint8_t attr = noFlags ? in.nr[0x6C] : vram(in.nr[0x6E], entry + 1);
            bool ulaOver;
            if (text)
            {
                paletteOffset = (attr >> 1) & 0x7F;
                xMirror = yMirror = rotate = false;
                ulaOver = (attr & 1) != 0;
            }
            else
            {
                paletteOffset = (attr >> 4) & 0x0F;
                xMirror = (attr & 0x08) != 0;
                yMirror = (attr & 0x04) != 0;
                rotate = (attr & 0x02) != 0;
                ulaOver = (attr & 1) != 0;
            }
            if (mode512)
            {
                tile |= (attr & 1) ? 0x100 : 0;
                ulaOver = false;
            }
            below = ulaOnTop ? false : (ulaOver || mode512);
            if (text)
                textRow = vram(in.nr[0x6F], tile * 8 + pixelY);
            else if (!rotate)
            {
                const unsigned py = yMirror ? 7 - pixelY : pixelY;
                for (unsigned i = 0; i < 4; i++)
                    row[i] = vram(in.nr[0x6F], tile * 32 + py * 4 + i);
            }
        }
        unsigned pixel;
        if (text)
            pixel = (textRow >> (7 - pixelX)) & 1;
        else
        {
            const unsigned px = (xMirror ^ rotate) ? 7 - pixelX : pixelX;
            const unsigned py = yMirror ? 7 - pixelY : pixelY;
            if (!rotate)
                pixel = (px & 1) ? (row[px >> 1] & 0x0F) : (row[px >> 1] >> 4);
            else  // the tile turned a quarter: rows and columns swap
            {
                const uint8_t byte = vram(in.nr[0x6F], tile * 32 + px * 4 + (py >> 1));
                pixel = (py & 1) ? (byte & 0x0F) : (byte >> 4);
            }
        }
        if (!text && pixel == transparentIndex)
            continue;
        const unsigned index = text ? ((paletteOffset << 1) | (pixel & 1)) : ((paletteOffset << 4) | (pixel & 0x0F));
        Pixel& p = line[sx];
        p.colour = regs.PaletteEntry(palette, index & 0xFF);
        p.opaque = true;
        p.below = below;
        p.textMode = text;
    }
}

/// Sprites: one grid line from the engine, two sub-pixels per pixel, the sprite palette (NR #43 bit 3 picks the second)
void NextVideoRenderer::SpriteLine(const NextVideoInputs& in, unsigned y, Pixel* line)
{
    if (!in.sprites)
        return;
    NextSprites::Pixel grid[NextSprites::kGridWidth];
    in.sprites->DrawLine(y, in.nr[0x15], *in.regs, grid, in.nr[0x4B]);
    const unsigned palette = (in.regs->PaletteControl() & 0x08) ? 6 : 2;
    for (unsigned x = 0; x < NextSprites::kGridWidth; x++)
    {
        if (!grid[x].opaque)
            continue;
        for (unsigned s = 0; s < 2; s++)
        {
            Pixel& p = line[x * 2 + s];
            p.colour = in.regs->PaletteEntry(palette, grid[x].index);
            p.opaque = true;
        }
    }
}

void NextVideoRenderer::RenderLine(const NextVideoInputs& in, unsigned y, uint32_t* out)
{
    Pixel ula[kW];
    Pixel layer2[kW];
    Pixel tiles[kW];
    Pixel sprites[kW];
    if (in.nr[0x15] & 0x80)
        LoResLine(in, y, ula);
    else
        UlaLine(in, y, ula);
    Layer2Line(in, y, layer2);
    TilemapLine(in, y, tiles);
    SpriteLine(in, y, sprites);

    // A pixel the layer paints is transparent when its 8 MSBs equal the global transparency colour (NR #14)
    const unsigned transparent = in.nr[0x14];
    auto visible = [&](const Pixel& p) { return p.opaque && ((p.colour >> 1) & 0xFF) != transparent; };

    // NR #15 bits 4:2, top layer first: 000 SLU, 001 LSU, 010 SUL, 011 LUS, 100 USL, 101 ULS; the blend modes (110,
    // 111) draw as SLU. In LUS / USL / ULS the ULA border over a transparent tilemap does not hide a sprite
    const unsigned order = (in.nr[0x15] >> 2) & 7;
    const uint16_t fallback = Nine(in.nr[0x4A]);
    for (unsigned x = 0; x < kW; x++)
    {
        // the ULA and the tilemap merge into one layer first: a tile pixel wins unless it is marked below the ULA
        // (text mode tiles are transparent by the global colour) and the ULA pixel is opaque
        const Pixel& t = tiles[x];
        const bool tv = t.opaque && !(t.textMode && ((t.colour >> 1) & 0xFF) == transparent);
        const bool ulaVisible = visible(ula[x]);
        Pixel merged = ula[x];
        bool uv = ulaVisible;
        if (tv && (!t.below || !ulaVisible))
        {
            merged = t;
            uv = true;
        }
        const Pixel& u = merged;
        const Pixel& l = layer2[x];
        const bool lv = visible(l);
        const Pixel& sp = sprites[x];
        const bool sv = sp.opaque;
        const bool borderException = ula[x].border && !tv && sv;
        const bool ue = uv && !(order >= 3 && order <= 5 && borderException);
        uint16_t colour = fallback;
        if (lv && (l.colour & 0x200))
            colour = l.colour;  // a Layer 2 priority colour is above everything
        else
        {
            // the three layers in the order of NR #15, top first: 'S' sprites, 'L' Layer 2, 'U' ULA + tilemap
            static const char* const kOrders[8] = {"SLU", "LSU", "SUL", "LUS", "USL", "ULS", "SLU", "SLU"};
            for (const char* layer = kOrders[order]; *layer; layer++)
            {
                if (*layer == 'S' && sv)
                    colour = sp.colour;
                else if (*layer == 'L' && lv)
                    colour = l.colour;
                else if (*layer == 'U' && ue)
                    colour = u.colour;
                else
                    continue;
                break;
            }
        }
        out[x] = Rgba(colour & 0x1FF);
    }
}
