#include "stdafx.h"

#include "nextvideorenderer.h"

#include <algorithm>
#include <cstring>

namespace
{
constexpr unsigned kW = NextVideoRenderer::kWidth;

uint16_t Nine(uint8_t rrrgggbb)
{
    return static_cast<uint16_t>((rrrgggbb << 1) | ((rrrgggbb & 3) ? 1 : 0));
}

/// The id of the winning layer of a pixel: tracked only when the layers are recorded; in the other instance every assignment vanishes
template <bool kRecord>
struct WinnerId
{
    uint8_t value = 0;
    WinnerId() = default;
    WinnerId(uint8_t v) { *this = v; }
    WinnerId& operator=(uint8_t v)
    {
        if constexpr (kRecord)
            value = v;
        return *this;
    }
};

bool GlobalTransparent(unsigned colour, unsigned transparent)
{
    return ((colour >> 1) & 0xFF) == transparent;
}

constexpr uint8_t Expand3(unsigned v)
{
    return static_cast<uint8_t>((v << 5) | (v << 2) | (v >> 1));
}

/// Every 9-bit colour as RGBA bytes: the compositor converts one per sub-pixel
struct RgbaTable
{
    uint32_t value[512];
    constexpr RgbaTable() : value()
    {
        for (unsigned c = 0; c < 512; c++)
        {
            const unsigned r = (c >> 6) & 7;
            const unsigned g = (c >> 3) & 7;
            const unsigned b = c & 7;
            value[c] = 0xFF000000u | (static_cast<uint32_t>(Expand3(b)) << 16) | (static_cast<uint32_t>(Expand3(g)) << 8) | Expand3(r);
        }
    }
};
constexpr RgbaTable kRgba;

/// The three layers in the order of NR #15 bits 4:2, top first: 'S' sprites, 'L' Layer 2, 'U' ULA + tilemap
constexpr char kOrders[6][4] = {"SLU", "LSU", "SUL", "LUS", "USL", "ULS"};
}  // namespace

void NextVideoRenderer::RecordSegment(NextLayer::Frame& frame, unsigned y, unsigned fromX, unsigned toX, const NextVideoInputs& in)
{
    if (frame.segments.size() >= 0xFFFF)
        return;
    NextLayer::Segment s;
    s.fromX = static_cast<uint16_t>(fromX);
    s.toX = static_cast<uint16_t>(toX);
    s.border = in.border;
    s.portFf = in.portFf;
    s.flags = static_cast<uint8_t>((in.shadowScreen ? NextLayer::Segment::kShadowScreen : 0) | (in.layer2Enable ? NextLayer::Segment::kLayer2Enable : 0) |
                                   (in.flash ? NextLayer::Segment::kFlash : 0));
    s.paletteControl = in.regs->PaletteControl();
    s.ulaNextFormat = in.regs->UlaNextFormat();
    for (unsigned w = 0; w < NextVideoRegs::kClipWindows; w++)
        for (unsigned c = 0; c < 4; c++)
            s.clip[w][c] = in.regs->Clip(w, c);
    for (unsigned i = 0; i < NextLayer::kKeyRegCount; i++)
        s.nr[i] = in.nr[NextLayer::kKeyRegs[i]];
    if (frame.lineSegments[y] == 0)
        frame.lineFirst[y] = static_cast<uint16_t>(frame.segments.size());
    frame.lineSegments[y]++;
    frame.segments.push_back(s);
}

uint32_t NextVideoRenderer::Rgba(uint16_t colour9)
{
    return kRgba.value[colour9 & 0x1FF];
}

namespace
{
/// UlaFetchAt for one paper row: what does not depend on the sub-pixel is worked out once (the renderer asks for all 512 of a row)
struct UlaRowFetch
{
    unsigned mode;
    unsigned bitmapRow, attrRow, pixelBase;
    unsigned scrollX, fine;

    UlaRowFetch(unsigned portFf, bool shadowScreen, unsigned scrollX_, unsigned scrollY, bool fineScroll, unsigned py)
    {
        // zxula.vhd: screen_mode_s = port #FF bits 2:0 unless the shadow screen is shown (bank 7 is an 8K BRAM: no Timex modes there)
        mode = shadowScreen ? 0 : (portFf & 7);
        // py_s = vc + scroll_y (9 bits), folded into 0..191 by +1 on y(7:6) from 192 up, and from 384 on 0..62 again: a modulo 192
        const unsigned sy = (py + scrollY) % 192;
        bitmapRow = ((sy & 0xC0) << 5) | ((sy & 7) << 8) | ((sy & 0x38) << 2);
        attrRow = 0x1800 + (sy >> 3) * 32;
        pixelBase = (mode & 1) ? 0x2000 : 0;
        scrollX = scrollX_;
        fine = fineScroll ? 1 : 0;
    }

    // mode bit 1 (hi-colour) reads the attribute at the bitmap's offset in screen 1, otherwise from the attribute area of screen `bit 0`
    unsigned AttrAt(unsigned column) const { return (mode & 2) ? 0x2000 + bitmapRow + column : pixelBase + attrRow + column; }

    NextVideoRenderer::UlaFetch At(unsigned sub) const
    {
        NextVideoRenderer::UlaFetch f;
        f.mode = mode;
        if (mode & 4)
        {
            // hi-res: the shift register holds [bitmap byte][second byte] of two columns, one bit per 14 MHz clock, shifted by 2 * scroll(2:0)
            // + fine: so the stream position is 2 * scroll + fine + sub, 16 bits per column, modulo the 32 columns
            const unsigned h = (sub + 2 * scrollX + fine) & 511;
            const unsigned column = h >> 4;
            f.bit = h & 7;
            if ((h >> 3) & 1)
                f.bitmapOffset = AttrAt(column);  // the second byte is the "attribute" fetch
            else
                f.bitmapOffset = pixelBase + bitmapRow + column;
            f.attrOffset = f.bitmapOffset;
            f.synthesised = true;
            return f;
        }
        // each bit is two clocks wide: the half-pixel fine scroll moves the picture by one clock, so sub-pixel `sub` shows pixel
        // scroll + (sub + fine) / 2 (the column wraps at 32: mod 256)
        const unsigned sx = (scrollX + ((sub + fine) >> 1)) & 255;
        f.bitmapOffset = pixelBase + bitmapRow + (sx >> 3);
        f.attrOffset = AttrAt(sx >> 3);
        f.bit = sx & 7;
        return f;
    }
};
}  // namespace

NextVideoRenderer::UlaFetch NextVideoRenderer::UlaFetchAt(unsigned portFf, bool shadowScreen, unsigned scrollX, unsigned scrollY, bool fineScroll,
                                                          unsigned py, unsigned sub)
{
    return UlaRowFetch(portFf, shadowScreen, scrollX, scrollY, fineScroll, py).At(sub);
}

unsigned NextVideoRenderer::Layer2WideColumn(unsigned hc, unsigned scrollX)
{
    const unsigned xPre = hc + scrollX;  // 10 bits
    const bool plain = (xPre & 0x200) == 0 && ((xPre & 0x100) == 0 || ((xPre >> 6) & 3) == 0);
    if (plain)
        return xPre & 0x1FF;
    return (((((xPre >> 6) & 7) + 3) & 7) << 6) | (xPre & 0x3F);
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
    // ULA+ (NR #68 bit 3 / port #FF3B): the 64-entry CLUT sits in entries 192-255 of the ULA palette; ULANext wins over it
    const bool ulaPlus = (in.nr[0x68] & 0x08) != 0 && !ulaNext;
    const unsigned transparent = in.nr[0x14];  // a pixel in this colour is transparent: opaque stays clear
    const unsigned timexMode = in.shadowScreen ? 0 : (in.portFf & 7);  // zxula.vhd: no Timex modes on the shadow screen
    const bool hiRes = (timexMode & 4) != 0;

    auto border = [&](Pixel& p) {
        if (fullInk)
            return;  // paper and border come from the fallback colour: transparent here
        // Timex hi-res: the attribute of the border is 01 PPP III of port #FF (zxula.vhd border_clr_tmx): paper = 7 - ink and the bright
        // bit set, so the standard palette's bright paper entry; ULANext takes 128 + paper
        const unsigned colour = hiRes ? 7 - ((in.portFf >> 3) & 7) : in.border;
        // ULA+: the border is the paper half of the CLUT's row of the attribute (zxula.vhd: 11 & attr(7:6) & 1 & attr(5:3); the border
        // attribute is 00 BBB BBB, the hi-res one 01 PPP III)
        const unsigned c = regs.PaletteEntry(palette, ulaPlus ? (hiRes ? 0xD8 : 0xC8) + colour : (ulaNext ? 128 + colour : 16 + (hiRes ? 8 : 0) + colour)) & 0x1FF;
        p = NextLayer::MakePixel(c, !GlobalTransparent(c, transparent), true);
    };
    // The border pixel is the same along the line: worked out once (with ULANext's full-ink format the row stays as it came)
    const bool inPaperRows = y >= kPaperTop && y < kPaperTop + 192;
    if (!fullInk)
    {
        Pixel borderPixel;
        border(borderPixel);
        if (!inPaperRows)
        {
            std::fill(line, line + kW, borderPixel);
            return;
        }
        std::fill(line, line + kPaperLeft, borderPixel);
        std::fill(line + kPaperLeft + 512, line + kW, borderPixel);
    }
    else if (!inPaperRows)
        return;

    const unsigned py = y - kPaperTop;
    const unsigned base = in.shadowScreen ? 7 : 5;
    const uint8_t* screen = in.ram + static_cast<size_t>(base) * 0x4000;
    const bool fine = (in.nr[0x68] & 0x04) != 0;  // NR #68 bit 2: ULA scroll in half pixels

    auto index = [&](bool ink, uint8_t attr) -> int {
        unsigned bright = (attr >> 6) & 1;
        bool inkPixel = ink;
        if ((attr & 0x80) && in.flash && !ulaNext && !ulaPlus)  // ULANext: the attribute is all colour index, no flashing; ULA+ none either
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
        if (ulaPlus)
        {
            // "11" & flash & bright & (hi-res or paper) & colour (zxula.vhd): the Timex hi-res ink takes the paper half too
            const unsigned half = (hiRes || !inkPixel) ? 8 : 0;
            const unsigned colour = inkPixel ? (attr & 7) : ((attr >> 3) & 7);
            return static_cast<int>(0xC0 | ((attr & 0xC0) >> 2) | half | colour);
        }
        return inkPixel ? static_cast<int>((attr & 7) + 8 * bright) : static_cast<int>(16 + ((attr >> 3) & 7) + 8 * bright);
    };
    // The line arrives zeroed: an index of -1 (nothing to paint) leaves the pixel transparent

    // hi-res attribute the ULA makes of port #FF: bright | paper (7 - ink) << 3 | ink, run through the same decoding as any attribute
    const unsigned ink = (in.portFf >> 3) & 7;
    const uint8_t tmxAttr = static_cast<uint8_t>(0x40 | ((7 - ink) << 3) | ink);
    unsigned lastBitmap = ~0u, lastBit = ~0u, lastAttr = ~0u;
    const UlaRowFetch row(in.portFf, in.shadowScreen, in.nr[0x26], in.nr[0x27], fine, py);
    // the pixel of the fetch in force: looked up in the palette when the fetch changes, not for every sub-pixel
    Pixel pixel;
    bool paint = false;
    Pixel* const paper = line + kPaperLeft;
    for (unsigned sub = 0; sub < 512; sub++)
    {
        const UlaFetch f = row.At(sub);
        if (f.bitmapOffset != lastBitmap || f.bit != lastBit || f.attrOffset != lastAttr)
        {
            lastBitmap = f.bitmapOffset;
            lastBit = f.bit;
            lastAttr = f.attrOffset;
            const bool inkPixel = ((screen[f.bitmapOffset] >> (7 - f.bit)) & 1) != 0;
            const int idx = index(inkPixel, f.synthesised ? tmxAttr : screen[f.attrOffset]);
            paint = idx >= 0;
            if (paint)
            {
                const unsigned c = static_cast<unsigned>(regs.PaletteEntry(palette, static_cast<unsigned>(idx)) & 0x1FF);
                pixel = NextLayer::MakePixel(c, !GlobalTransparent(c, transparent));
            }
        }
        if (paint)
            paper[sub] = pixel;
    }
}

/// LoRes (NR #15 bit 7): 128 x 96 at 256 colours of the ULA palette, in the two halves of bank 5 (zxnext.vhd reads it from bank 5
/// whatever the shadow screen bit says); with NR #6A bit 5 the Radastan mode: 128 x 96 at 4 bits, a 64-byte row of two nibbles per four
/// pixels, the display file of port #FF bit 0 xor NR #6A bit 4 (lores.vhd)
void NextVideoRenderer::LoResLine(const NextVideoInputs& in, unsigned y, Pixel* line)
{
    const NextVideoRegs& regs = *in.regs;
    if (in.nr[0x68] & 0x80 || y < kPaperTop || y >= kPaperTop + 192)
        return;
    const unsigned palette = (regs.PaletteControl() & 0x02) ? 4 : 0;
    const unsigned offset = (in.nr[0x6A] & 0x0F) << 4;
    const unsigned transparent = in.nr[0x14];
    // lores.vhd: y_pre = vc + scroll_y (9 bits); y(7:6) = y_pre(7:6) + 1 from 192 up, y(5:0) = y_pre(5:0): the row runs on into the
    // memory behind the 96 rows (up to y = 254) instead of wrapping at 192
    const unsigned yPre = (y - kPaperTop) + in.nr[0x33];
    const unsigned yy = (yPre >= 192) ? (((((yPre >> 6) & 3) + 1) & 3) << 6) | (yPre & 0x3F) : (yPre & 0xFF);
    const unsigned row = yy >> 1;  // y(7:1): 0-127
    // addr(13:11) + 1 once y >= 96 (3 bits): the second half of the bitmap starts at #2000 instead of #1800
    const unsigned rowAddress = row << 7;
    const unsigned high = (yy >= 96) ? ((((rowAddress >> 11) & 7) + 1) & 7) : ((rowAddress >> 11) & 7);
    const unsigned rowBase = (high << 11) | (rowAddress & 0x7FF);
    const uint8_t* screen = in.ram + static_cast<size_t>(5) * 0x4000;
    const unsigned scrollX = in.nr[0x32];  // NR #32: added to the pixel x, 8 bits; the byte is x(7:1)
    const bool radastan = (in.nr[0x6A] & 0x20) != 0;
    const bool ulaPlus = (in.nr[0x68] & 0x08) != 0 && (regs.PaletteControl() & 0x01) == 0;
    const unsigned dfile = ((in.portFf & 1) ^ ((in.nr[0x6A] >> 4) & 1)) != 0 ? 0x2000 : 0;
    for (unsigned px = 0; px < 256; px++)
    {
        const unsigned x = (px + scrollX) & 255;
        unsigned idx;
        if (radastan)
        {
            const unsigned address = (dfile + row * 64 + (x >> 2)) & 0x3FFF;
            const uint8_t byte = screen[address];
            const unsigned nibble = (x & 2) == 0 ? (byte >> 4) : (byte & 0x0F);
            // the high nibble of the palette index: the offset, or "11" & offset(1:0) when ULA+ is on (the CLUT)
            idx = ((ulaPlus ? (0x0C | (offset >> 4 & 3)) : (offset >> 4)) << 4) | nibble;
        }
        else
            idx = (screen[(rowBase | (x >> 1)) & 0x3FFF] + offset) & 0xFF;
        Pixel& a = line[kPaperLeft + px * 2];
        const unsigned c = regs.PaletteEntry(palette, idx) & 0x1FF;
        a = NextLayer::MakePixel(c, !GlobalTransparent(c, transparent));
        line[kPaperLeft + px * 2 + 1] = a;
    }
}

/// Layer 2: 256 x 192 x 8 (row-major), 320 x 256 x 8 and 640 x 256 x 4 (column-major), from NR #12's bank
bool NextVideoRenderer::Layer2Line(const NextVideoInputs& in, unsigned y, Pixel* line)
{
    if (!in.layer2Enable)
        return false;
    const NextVideoRegs& regs = *in.regs;
    const unsigned resolution = (in.nr[0x70] >> 4) & 3;
    const unsigned palette = (regs.PaletteControl() & 0x04) ? 5 : 1;  // NR #43 bit 2: the second Layer 2 palette
    const unsigned offset = (in.nr[0x70] & 0x0F) << 4;
    const unsigned bank = in.nr[0x12] & 0x7F;
    const unsigned scrollX = in.nr[0x16] | ((in.nr[0x71] & 1) << 8);
    const unsigned scrollY = in.nr[0x17];
    const unsigned transparent = in.nr[0x14];
    const uint8_t* base = in.ram + static_cast<size_t>(bank) * 0x4000;
    const size_t limit = static_cast<size_t>(in.ramPages) * 0x4000;
    if (static_cast<size_t>(bank) * 0x4000 >= limit)
        return true;
    const unsigned x1 = regs.Clip(0, 0), x2 = regs.Clip(0, 1), y1 = regs.Clip(0, 2), y2 = regs.Clip(0, 3);
    auto available = [&](size_t address) { return static_cast<size_t>(bank) * 0x4000 + address < limit; };

    if (resolution == 0)
    {
        if (y < kPaperTop || y >= kPaperTop + 192)
            return true;
        const unsigned py = y - kPaperTop;
        if (py < y1 || py > y2)
            return true;
        // layer2.vhd: y_pre = vc + scroll_y (9 bits); y(7:6) is y_pre(7:6), +1 once y_pre reaches 192 (that wraps 192-255 to 0-63,
        // 256-319 to 64-127, 320-383 to 128-191 and 384 on to rows 192-254 of the bank: not a modulo for a scroll above 191)
        const unsigned yPre = py + scrollY;
        const unsigned yHigh = ((yPre & 0x100) == 0 && ((yPre >> 6) & 3) != 3) ? ((yPre >> 6) & 3) : (((yPre >> 6) & 3) + 1) & 3;
        const unsigned sy = (yHigh << 6) | (yPre & 0x3F);
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
                p = NextLayer::MakePixel(colour, !GlobalTransparent(colour, transparent));
            }
        }
        return true;
    }
    // the whole 320 x 256 grid; the clip's X units are two pixels, its Y the line
    if (y < y1 || y > y2)
        return true;
    const unsigned sy = (y + scrollY) & 255;
    if (resolution == 1)
    {
        for (unsigned px = x1 * 2; px <= x2 * 2 + 1 && px < 320; px++)
        {
            const unsigned sx = Layer2WideColumn(px, scrollX);
            const size_t address = static_cast<size_t>(sx) * 256 + sy;
            if (!available(address))
                continue;
            const unsigned idx = (base[address] + offset) & 0xFF;
            const uint16_t colour = regs.PaletteEntry(palette, idx);
            for (unsigned s = 0; s < 2; s++)
            {
                Pixel& p = line[px * 2 + s];
                p = NextLayer::MakePixel(colour, !GlobalTransparent(colour, transparent));
            }
        }
        return true;
    }
    // 640 x 256 x 4: two nibbles per byte, the high nibble on the left
    for (unsigned px = x1 * 4; px <= x2 * 4 + 3 && px < 640; px++)
    {
        const unsigned column = Layer2WideColumn(px >> 1, scrollX);  // the byte of the 7 MHz pixel, its two nibbles are the sub-pixels
        const size_t address = static_cast<size_t>(column) * 256 + sy;
        if (!available(address))
            continue;
        const unsigned nibble = (px & 1) ? (base[address] & 0x0F) : (base[address] >> 4);
        const unsigned idx = ((offset & 0xF0) | nibble) & 0xFF;
        Pixel& p = line[px];
        const unsigned c = regs.PaletteEntry(palette, idx);
        p = NextLayer::MakePixel(c, !GlobalTransparent(c, transparent));
    }
    return true;
}

/// The tilemap (NR #6B-#6F, #2F-#31, #4C): 40 or 80 columns of 8 x 8 tiles of 4-bit pixels (or 1-bit text tiles) read
/// from bank 5 / 7 at 256-byte offsets, scrolled and wrapped, with mirror / rotate / palette offset per tile
bool NextVideoRenderer::TilemapLine(const NextVideoInputs& in, unsigned y, Pixel* line)
{
    const uint8_t control = in.nr[0x6B];
    if (!(control & 0x80))
        return false;
    const NextVideoRegs& regs = *in.regs;
    if (y < regs.Clip(3, 2) || y > regs.Clip(3, 3))
        return true;
    const bool wide = (control & 0x40) != 0;  // 80 columns
    const bool noFlags = (control & 0x20) != 0;
    const bool text = (control & 0x08) != 0;
    const bool mode512 = (control & 0x02) != 0;
    const bool ulaOnTop = (control & 0x01) != 0;
    const unsigned palette = (control & 0x10) ? 7 : 3;
    const unsigned transparentIndex = in.nr[0x4C] & 0x0F;
    const unsigned transparent = in.nr[0x14];
    const unsigned scrollX = in.nr[0x30] | ((in.nr[0x2F] & 3) << 8);
    const unsigned scrollY = in.nr[0x31];
    auto vram = [&](uint8_t base, unsigned offset) -> uint8_t {
        // zxnext.vhd: "ULA BANK 7 (8k only due to limited bram resources)" - the video side of bank 7 is the 8K BRAM of page #0E, addressed
        // by bits 12:0, so a map placed at #2000 of bank 7 reads (and a CPU write through MMU page #0E lands) at #0000
        const bool bank7 = (base & 0x80) != 0;
        const unsigned page = bank7 ? 7 : 5;
        const size_t address = static_cast<size_t>(page) * 0x4000 + ((static_cast<size_t>(base & 0x3F) * 256 + offset) & (bank7 ? 0x1FFF : 0x3FFF));
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
        const unsigned c = regs.PaletteEntry(palette, index & 0xFF);
        // a text-mode pixel in the global colour is transparent
        p = NextLayer::MakePixel(c, !(text && GlobalTransparent(c, transparent)), false, below, text);
    }
    return true;
}

/// Sprites: one grid line from the engine, two sub-pixels per pixel, the sprite palette (NR #43 bit 3 picks the second)
bool NextVideoRenderer::SpriteLine(const NextVideoInputs& in, unsigned y, Pixel* line, uint8_t* numbers)
{
    if (!in.sprites)
        return false;
    NextSprites::Pixel grid[NextSprites::kGridWidth];
    if (in.spriteLine)  // the engine's fill of this row; the enable, the clip and the palette are the output stage's, as they are now
        in.sprites->Output(*in.spriteLine, y, in.nr[0x15], *in.regs, grid);
    else
        in.sprites->DrawLine(y, in.nr[0x15], *in.regs, grid, in.nr[0x4B], in.lineClocks);
    const unsigned palette = (in.regs->PaletteControl() & 0x08) ? 6 : 2;
    bool painted = false;
    for (unsigned x = 0; x < NextSprites::kGridWidth; x++)
    {
        if (!grid[x].opaque)
            continue;
        painted = true;
        for (unsigned s = 0; s < 2; s++)
        {
            Pixel& p = line[x * 2 + s];
            p = NextLayer::MakePixel(in.regs->PaletteEntry(palette, grid[x].index), true);
            if (numbers)
                numbers[x * 2 + s] = grid[x].sprite;
        }
    }
    return painted;
}

/// The ULA clip window (NR #1A, in paper pixels): what it excludes of the paper is transparent. The border is never clipped:
/// zxula.vhd o_ula_clipped is '0' when the pixel is inside the window "or border_active = '1'" (the LoRes layer has no window of
/// its own: lores.vhd gets the same one and a pixel outside it falls back to the clipped ULA pixel)
void NextVideoRenderer::ApplyUlaClip(const NextVideoInputs& in, unsigned y, Pixel* line)
{
    const NextVideoRegs& regs = *in.regs;
    const unsigned x1 = regs.Clip(2, 0), x2 = regs.Clip(2, 1), y1 = regs.Clip(2, 2), y2 = regs.Clip(2, 3);
    if (x1 == 0 && x2 >= 255 && y1 == 0 && y2 >= 191)
        return;  // the default window: nothing is clipped
    if (y < kPaperTop || y >= kPaperTop + 192)
        return;  // border rows
    const unsigned py = y - kPaperTop;
    if (py < y1 || py > y2)
    {
        for (unsigned x = kPaperLeft; x < kPaperLeft + 512; x++)
            line[x] = Pixel();
        return;
    }
    for (unsigned px = 0; px < 256; px++)
        if (px < x1 || px > x2)
        {
            line[kPaperLeft + px * 2] = Pixel();
            line[kPaperLeft + px * 2 + 1] = Pixel();
        }
}

unsigned NextVideoRenderer::DrawLayers(const NextVideoInputs& in, unsigned y, Pixel* ula, Pixel* layer2, Pixel* tiles, Pixel* sprites,
                                       uint8_t* spriteNumbers)
{
    // LoRes replaces the paper of the ULA, whose border stays
    UlaLine(in, y, ula);
    if (in.nr[0x15] & 0x80)
        LoResLine(in, y, ula);
    ApplyUlaClip(in, y, ula);
    unsigned above = 0;
    if (Layer2Line(in, y, layer2))
        above |= kAboveLayer2;
    if (TilemapLine(in, y, tiles))
        above |= kAboveTilemap;
    if (SpriteLine(in, y, sprites, spriteNumbers))  // always run: with no line buffer the engine sets its flags by drawing
        above |= kAboveSprites;
    return above;
}

void NextVideoRenderer::RenderLine(const NextVideoInputs& in, unsigned y, uint32_t* out)
{
    if (const NextLayer::LineSink* sink = in.layerOut)
    {
        // The layers are drawn straight into the sink's rows: recording them costs no extra pass
        for (Pixel* row : sink->plane)
            std::memset(static_cast<void*>(row), 0, kW * sizeof(Pixel));
        uint8_t spriteNumbers[kW];
        const unsigned above = DrawLayers(in, y, sink->plane[NextLayer::kUla], sink->plane[NextLayer::kLayer2],
                                          sink->plane[NextLayer::kTilemap], sink->plane[NextLayer::kSprites], spriteNumbers);
        ComposeLine<true>(in, above, sink->plane[NextLayer::kUla], sink->plane[NextLayer::kLayer2], sink->plane[NextLayer::kTilemap],
                          sink->plane[NextLayer::kSprites], spriteNumbers, out);
        return;
    }
    Pixel ula[kW];
    Pixel layer2[kW];
    Pixel tiles[kW];
    Pixel sprites[kW];
    const unsigned above = DrawLayers(in, y, ula, layer2, tiles, sprites, nullptr);
    ComposeLine<false>(in, above, ula, layer2, tiles, sprites, nullptr, out);
}

template <bool kRecord>
void NextVideoRenderer::ComposeUlaOnly(const NextVideoInputs& in, const Pixel* ula, uint32_t* out)
{
    [[maybe_unused]] const NextLayer::LineSink* sink = in.layerOut;
    const uint32_t fallback = kRgba.value[Nine(in.nr[0x4A]) & 0x1FF];
    for (unsigned x = 0; x < kW; x++)
    {
        const Pixel p = ula[x];
        out[x] = p.opaque ? kRgba.value[p.colour & 0x1FF] : fallback;
        if constexpr (kRecord)
        {
            sink->ids[x] = p.opaque ? (p.border ? NextLayer::kIdBorder : NextLayer::kIdUla) : NextLayer::kFallback;
            sink->info[x] = 0;
        }
    }
}

template <bool kRecord>
void NextVideoRenderer::ComposeLine(const NextVideoInputs& in, unsigned above, const Pixel* ula, const Pixel* layer2, const Pixel* tiles,
                                    const Pixel* sprites, const uint8_t* spriteNumbers, uint32_t* out)
{
    // NR #15 bits 4:2, top layer first: 000 SLU, 001 LSU, 010 SUL, 011 LUS, 100 USL, 101 ULS; modes 110 / 111 blend
    // Layer 2 with the ULA / tilemap
    const unsigned order = (in.nr[0x15] >> 2) & 7;
    // Nothing above the ULA on this line (the common screen: Layer 2, the tilemap and the sprites off or not on this line). The blend
    // orders and the stencil combine the ULA with the empty layers in their own way: they take the general loop
    const bool stencil = (in.nr[0x68] & 0x01) && (in.nr[0x6B] & 0x80) && !(in.nr[0x68] & 0x80);
    if (above == 0 && order < 6 && !stencil)
    {
        ComposeUlaOnly<kRecord>(in, ula, out);
        return;
    }
    switch (order)
    {
        case 0: ComposeOrder<kRecord, 0>(in, ula, layer2, tiles, sprites, spriteNumbers, out); break;
        case 1: ComposeOrder<kRecord, 1>(in, ula, layer2, tiles, sprites, spriteNumbers, out); break;
        case 2: ComposeOrder<kRecord, 2>(in, ula, layer2, tiles, sprites, spriteNumbers, out); break;
        case 3: ComposeOrder<kRecord, 3>(in, ula, layer2, tiles, sprites, spriteNumbers, out); break;
        case 4: ComposeOrder<kRecord, 4>(in, ula, layer2, tiles, sprites, spriteNumbers, out); break;
        case 5: ComposeOrder<kRecord, 5>(in, ula, layer2, tiles, sprites, spriteNumbers, out); break;
        case 6: ComposeOrder<kRecord, 6>(in, ula, layer2, tiles, sprites, spriteNumbers, out); break;
        default: ComposeOrder<kRecord, 7>(in, ula, layer2, tiles, sprites, spriteNumbers, out); break;
    }
}

template <bool kRecord, unsigned kOrder>
void NextVideoRenderer::ComposeOrder(const NextVideoInputs& in, const Pixel* ula, const Pixel* layer2, const Pixel* tiles, const Pixel* sprites,
                                     const uint8_t* spriteNumbers, uint32_t* out)
{
    [[maybe_unused]] const NextLayer::LineSink* sink = in.layerOut;
    // A pixel the layer paints is transparent when its 8 MSBs equal the global transparency colour (NR #14): the layer functions
    // leave `opaque` clear for those, so a pixel is visible when it is opaque
    auto visible = [](const Pixel& p) { return p.opaque != 0; };

    // In LUS / USL / ULS the ULA border over a transparent tilemap does not hide a sprite
    constexpr unsigned order = kOrder;
    const uint16_t fallback = Nine(in.nr[0x4A]);
    // NR #68 bit 0: stencil (the ULA and tilemap pixels AND together), bits 6:5: the blend of modes 110 / 111
    const bool stencilMode = (in.nr[0x68] & 0x01) && (in.nr[0x6B] & 0x80) && !(in.nr[0x68] & 0x80);
    const unsigned blend = (in.nr[0x68] >> 5) & 3;
    auto r3 = [](uint16_t c) { return static_cast<unsigned>((c >> 6) & 7); };
    auto g3 = [](uint16_t c) { return static_cast<unsigned>((c >> 3) & 7); };
    auto b2 = [](uint16_t c) { return static_cast<unsigned>((c >> 1) & 3); };
    auto pack = [](unsigned r, unsigned g, unsigned b) { return static_cast<uint16_t>((r << 6) | (g << 3) | (b << 1) | (b ? 1 : 0)); };
    for (unsigned x = 0; x < kW; x++)
    {
        // the ULA and the tilemap merge into one layer first: a tile pixel wins unless it is marked below the ULA
        // (text mode tiles are transparent by the global colour) and the ULA pixel is opaque
        const Pixel& t = tiles[x];
        const bool tv = t.opaque != 0;
        const bool ulaVisible = visible(ula[x]);
        Pixel merged = ula[x];
        bool uv = ulaVisible;
        WinnerId<kRecord> uId = ula[x].border ? NextLayer::kIdBorder : NextLayer::kIdUla;  // what the merged ULA + tilemap layer is, for the record
        if (stencilMode)
        {
            uId = NextLayer::kIdStencil;
            uv = ulaVisible && tv;
            merged.opaque = uv;
            if (uv)
                merged.colour = pack(r3(ula[x].colour) & r3(t.colour), g3(ula[x].colour) & g3(t.colour), b2(ula[x].colour) & b2(t.colour));
        }
        else if (tv && (!t.below || !ulaVisible))
        {
            merged = t;
            uv = true;
            uId = NextLayer::kIdTilemap;
        }
        const Pixel& u = merged;
        const Pixel& l = layer2[x];
        const bool lv = visible(l);
        const Pixel& sp = sprites[x];
        const bool sv = sp.opaque != 0;
        const bool borderException = ula[x].border && !tv && sv;
        const bool ue = uv && !(order >= 3 && order <= 5 && borderException);
        uint16_t colour = fallback;
        WinnerId<kRecord> id = NextLayer::kFallback;
        const bool l2Priority = lv && (l.colour & 0x200);
        if constexpr (kOrder >= 6)
        {
            // Layer 2 added to / subtracted from the "mix" layer (the ULA, the tilemap or the stencil, by the blend
            // bits), with the tile or the ULA above or below it
            const bool below = t.below;
            Pixel mixRgb, mixTop, mixBot;  // opaque = not transparent
            const WinnerId<kRecord> ulaId = ula[x].border ? NextLayer::kIdBorder : NextLayer::kIdUla;
            WinnerId<kRecord> topId = ulaId.value, botId = ulaId.value;  // what mixTop / mixBot are, for the record
            switch (blend)
            {
                case 0:
                    mixRgb = ula[x];
                    mixRgb.opaque = ulaVisible;
                    mixTop = t;
                    mixTop.opaque = tv && !below;
                    mixBot = t;
                    mixBot.opaque = tv && below;
                    topId = botId = NextLayer::kIdTilemap;
                    break;
                case 2:
                    mixRgb = u;
                    mixRgb.opaque = uv;
                    break;
                case 3:
                    mixRgb = t;
                    mixRgb.opaque = tv;
                    mixTop = ula[x];
                    mixTop.opaque = ulaVisible && below;
                    mixBot = ula[x];
                    mixBot.opaque = ulaVisible && !below;
                    break;
                default:
                    if (below)
                    {
                        mixTop = ula[x];
                        mixTop.opaque = ulaVisible;
                        mixBot = t;
                        mixBot.opaque = tv;
                        botId = NextLayer::kIdTilemap;
                    }
                    else
                    {
                        mixTop = t;
                        mixTop.opaque = tv;
                        mixBot = ula[x];
                        mixBot.opaque = ulaVisible;
                        topId = NextLayer::kIdTilemap;
                    }
                    break;
            }
            const unsigned lr = lv ? r3(l.colour) : 0, lg = lv ? g3(l.colour) : 0, lb = lv ? b2(l.colour) : 0;
            const unsigned mr = mixRgb.opaque ? r3(mixRgb.colour) : 0, mg = mixRgb.opaque ? g3(mixRgb.colour) : 0,
                           mb = mixRgb.opaque ? b2(mixRgb.colour) : 0;
            unsigned rs = lr + mr, gs = lg + mg, bs = lb + mb;
            uint16_t mixer;
            if constexpr (kOrder == 6)
                mixer = pack(rs > 7 ? 7 : rs, gs > 7 ? 7 : gs, bs > 3 ? 3 : bs);
            else
            {
                if (mixRgb.opaque)
                {
                    auto sub = [](unsigned v) -> unsigned {
                        if (v <= 4)
                            return 0;
                        if (((v >> 2) & 3) == 3)
                            return 7;
                        return (v + 0x0B) & 0x0F;
                    };
                    rs = sub(rs);
                    gs = sub(gs);
                    bs = sub(bs);
                }
                mixer = pack(rs & 7, gs & 7, bs & 3);
            }
            if (l2Priority)
            {
                colour = mixer;
                id = NextLayer::kIdBlend;
            }
            else if (mixTop.opaque)
            {
                colour = mixTop.colour;
                id = topId.value;
            }
            else if (sv)
            {
                colour = sp.colour;
                id = NextLayer::kIdSprite;
            }
            else if (mixBot.opaque)
            {
                colour = mixBot.colour;
                id = botId.value;
            }
            else if (lv)
            {
                colour = mixer;
                id = NextLayer::kIdBlend;
            }
        }
        else if (l2Priority)
        {
            colour = l.colour;  // a Layer 2 priority colour is above everything
            id = NextLayer::kIdLayer2;
        }
        else
        {
            // the three layers in the order of NR #15, top first (a constant here: the walk folds into three tests)
            for (unsigned i = 0; i < 3; i++)
            {
                const char layer = kOrders[order < 6 ? order : 0][i];
                if (layer == 'S' && sv)
                {
                    colour = sp.colour;
                    id = NextLayer::kIdSprite;
                }
                else if (layer == 'L' && lv)
                {
                    colour = l.colour;
                    id = NextLayer::kIdLayer2;
                }
                else if (layer == 'U' && ue)
                {
                    colour = u.colour;
                    id = uId.value;
                }
                else
                    continue;
                break;
            }
        }
        out[x] = kRgba.value[colour & 0x1FF];
        if constexpr (kRecord)
        {
            sink->ids[x] = id.value;
            sink->info[x] = sv ? spriteNumbers[x] : 0;
        }
    }
}
