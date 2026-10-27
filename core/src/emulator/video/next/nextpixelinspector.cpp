#include "stdafx.h"

#include "nextpixelinspector.h"

#include <algorithm>

#include "common/stringhelper.h"
#include "emulator/video/next/nextlayerimage.h"
#include "emulator/video/next/nextvideorenderer.h"

namespace NextLayer
{
namespace
{
constexpr unsigned kPaperLeft = NextVideoRenderer::kPaperLeft;
constexpr unsigned kPaperTop = NextVideoRenderer::kPaperTop;

std::string Hex(unsigned value, unsigned digits)
{
    return StringHelper::Format("0x%0*X", static_cast<int>(digits), value);
}

std::string Rgb(uint16_t colour9)
{
    const uint32_t c = NextVideoRenderer::Rgba(colour9 & 0x1FF);
    return StringHelper::Format("#%02X%02X%02X", c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF);
}

uint16_t Nine(uint8_t rrrgggbb)
{
    return static_cast<uint16_t>((rrrgggbb << 1) | ((rrrgggbb & 3) ? 1 : 0));
}

unsigned R3(uint16_t c) { return (c >> 6) & 7; }
unsigned G3(uint16_t c) { return (c >> 3) & 7; }
unsigned B2(uint16_t c) { return (c >> 1) & 3; }
uint16_t Pack(unsigned r, unsigned g, unsigned b)
{
    return static_cast<uint16_t>((r << 6) | (g << 3) | (b << 1) | (b ? 1 : 0));
}

const char* const kOrders[8] = {"SLU", "LSU", "SUL", "LUS", "USL", "ULS", "blend", "blend"};

/// One pixel of the record with its segment, and the recomputation of where each layer's pixel came from
class Inspector
{
public:
    Inspector(const Frame& frame, const InspectEnv& env, unsigned x, unsigned y, unsigned sx)
        : _frame(frame), _env(env), _x(x), _y(y), _sub(x * 2 + sx), _at(static_cast<size_t>(y) * kWidth + x * 2 + sx)
    {
        _segment = frame.SegmentAt(y, _sub);
    }

    bool HasSegment() const { return _segment != nullptr; }
    StateNode Run();

private:
    // region <registers of the segment>
    uint8_t Nr(uint8_t reg) const { return _segment->Reg(reg); }
    unsigned Window(unsigned window, unsigned coordinate) const { return _segment->clip[window][coordinate]; }
    bool Shadow() const { return (_segment->flags & Segment::kShadowScreen) != 0; }
    // endregion

    uint8_t Ram(size_t address, bool& inRam) const
    {
        inRam = _env.ram && address < static_cast<size_t>(_env.ramPages) * 0x4000;
        return inRam ? _env.ram[address] : 0;
    }
    uint8_t Vram(uint8_t base, unsigned offset) const  // the tilemap's view of bank 5 / 7 (bank 7 is an 8K RAM for the video side)
    {
        const bool bank7 = (base & 0x80) != 0;
        const size_t address = static_cast<size_t>(bank7 ? 7 : 5) * 0x4000 + ((static_cast<size_t>(base & 0x3F) * 256 + offset) & (bank7 ? 0x1FFF : 0x3FFF));
        bool inRam;
        return Ram(address, inRam);
    }
    uint16_t PaletteColour(unsigned palette, unsigned index) const
    {
        return _env.regs ? static_cast<uint16_t>(_env.regs->PaletteEntry(palette, index) & 0x1FF) : 0;
    }

    StateNode UlaLayer(Pixel pixel, bool& clipped);
    StateNode Layer2Layer(Pixel pixel, bool& clipped);
    StateNode TilemapLayer(Pixel pixel, bool& clipped);
    StateNode SpritesLayer(Pixel pixel, bool& clipped);
    bool UlaClipped() const;
    std::string LostBecause(unsigned layer, unsigned winner, Pixel pixel) const;

    const Frame& _frame;
    const InspectEnv& _env;
    unsigned _x, _y, _sub;
    size_t _at;
    const Segment* _segment = nullptr;
    // filled by the layer builders, read by the "why" of the others
    bool _tileBelow = false;
};

/// The ULA clip window (NR #1A) as ApplyUlaClip applies it: is this sub-pixel cleared by it (only the paper: the border is never clipped)
bool Inspector::UlaClipped() const
{
    const unsigned x1 = Window(2, 0), x2 = Window(2, 1), y1 = Window(2, 2), y2 = Window(2, 3);
    if (x1 == 0 && x2 >= 255 && y1 == 0 && y2 >= 191)
        return false;
    const bool inRows = _y >= kPaperTop && _y < kPaperTop + 192;
    if (!inRows || _sub < kPaperLeft || _sub >= kPaperLeft + 512)
        return false;
    const unsigned py = _y - kPaperTop;
    if (py < y1 || py > y2)
        return true;
    const unsigned px = (_sub - kPaperLeft) / 2;
    return px < x1 || px > x2;
}

StateNode Inspector::UlaLayer(Pixel pixel, bool& clipped)
{
    StateNode source = StateNode::Object();
    clipped = false;
    const bool lores = (Nr(0x15) & 0x80) != 0;
    const bool ulaNext = (_segment->paletteControl & 0x01) != 0;
    const unsigned palette = (_segment->paletteControl & 0x02) ? 4 : 0;
    const uint8_t mask = _segment->ulaNextFormat;
    const bool fullInk = ulaNext && mask == 0xFF;
    const bool inPaper = _y >= kPaperTop && _y < kPaperTop + 192 && _sub >= kPaperLeft && _sub < kPaperLeft + 512;
    const unsigned timexMode = Shadow() ? 0 : (_segment->portFf & 7);  // no Timex modes on the shadow screen
    const bool hiRes = (timexMode & 4) != 0;
    if (Nr(0x68) & 0x80)
    {
        source["kind"] = "disabled";
        return source;
    }
    clipped = UlaClipped();
    if (!inPaper)
    {
        source["kind"] = "border";
        const unsigned colour = hiRes ? 7 - ((_segment->portFf >> 3) & 7) : _segment->border;
        source["colour"] = int(colour);
        source["palette_index"] = int(ulaNext ? 128 + colour : 16 + (hiRes ? 8 : 0) + colour);
        source["palette"] = int(palette);
        if (fullInk)
            source["note"] = "ULANext full-ink mode: paper and border come from the fallback colour";
        else if (hiRes)
            source["note"] = "Timex hi-res: the border takes the (bright) paper colour of port #FF";
        else
            source["note"] = "port #FE bits 2:0";
        return source;
    }
    const unsigned py = _y - kPaperTop;
    if (lores)
    {
        const unsigned px = (_sub - kPaperLeft) / 2;
        // lores.vhd, always bank 5: y_pre = vc + NR #33, +1 on y(7:6) from 192 up, +1 on addr(13:11) from y = 96
        const unsigned yPre = py + Nr(0x33);
        const unsigned yy = (yPre >= 192) ? (((((yPre >> 6) & 3) + 1) & 3) << 6) | (yPre & 0x3F) : (yPre & 0xFF);
        const unsigned rowAddress = (yy >> 1) << 7;
        const unsigned high = (yy >= 96) ? ((((rowAddress >> 11) & 7) + 1) & 7) : ((rowAddress >> 11) & 7);
        const unsigned column = ((px + Nr(0x32)) & 255) >> 1;
        const unsigned offset = (((high << 11) | (rowAddress & 0x7FF)) | column) & 0x3FFF;
        bool inRam;
        const size_t base = static_cast<size_t>(5) * 0x4000;
        const uint8_t byte = Ram(base + offset, inRam);
        source["kind"] = "lores";
        source["ram_page"] = 5;
        source["offset"] = Hex(offset, 4);
        source["byte"] = Hex(byte, 2);
        source["palette_index"] = int((byte + ((Nr(0x6A) & 0x0F) << 4)) & 0xFF);
        source["palette"] = int(palette);
        return source;
    }
    const size_t base = static_cast<size_t>(Shadow() ? 7 : 5) * 0x4000;
    bool inRam;
    const NextVideoRenderer::UlaFetch fetch = NextVideoRenderer::UlaFetchAt(_segment->portFf, Shadow(), Nr(0x26), Nr(0x27), (Nr(0x68) & 0x04) != 0, py, _sub - kPaperLeft);
    const unsigned bitmapOffset = fetch.bitmapOffset;
    const unsigned bit = fetch.bit;
    const unsigned attrOffset = fetch.synthesised ? 0xFFFF : fetch.attrOffset;  // hi-res: synthesised from port #FF, no byte
    uint8_t attr;
    if (fetch.synthesised)
    {
        const unsigned ink = (_segment->portFf >> 3) & 7;
        attr = static_cast<uint8_t>(0x40 | ((7 - ink) << 3) | ink);
    }
    else
        attr = Ram(base + attrOffset, inRam);
    const uint8_t byte = Ram(base + bitmapOffset, inRam);
    const bool ink = ((byte >> (7 - bit)) & 1) != 0;
    // the palette index as UlaLine's index() decides it
    bool inkPixel = ink;
    if ((attr & 0x80) && (_segment->flags & Segment::kFlash) && !ulaNext)
        inkPixel = !inkPixel;
    int index;
    const unsigned bright = (attr >> 6) & 1;
    if (ulaNext)
    {
        unsigned bits = 0;
        for (unsigned m = mask; m & 1; m >>= 1)
            bits++;
        const bool solidMask = mask && ((mask + 1) & mask) == 0;
        if (fullInk)
            index = inkPixel ? attr : -1;
        else if (!solidMask)
            index = inkPixel ? (attr & mask) : -1;
        else
            index = inkPixel ? (attr & mask) : 128 + (attr >> bits);
    }
    else
        index = inkPixel ? static_cast<int>((attr & 7) + 8 * bright) : static_cast<int>(16 + ((attr >> 3) & 7) + 8 * bright);
    source["kind"] = hiRes ? "paper_hires" : ((timexMode & 2) ? "paper_hicolour" : "paper");
    source["ram_page"] = Shadow() ? 7 : 5;
    source["bitmap_offset"] = Hex(bitmapOffset, 4);
    source["bitmap_byte"] = Hex(byte, 2);
    source["bit"] = int(7 - bit);
    source["ink_pixel"] = ink;
    if (attrOffset != 0xFFFF)
    {
        source["attribute_offset"] = Hex(attrOffset, 4);
        source["attribute_byte"] = Hex(attr, 2);
    }
    else
        source["attribute_byte"] = Hex(attr, 2) + " (synthesised from port #FF)";
    if (index >= 0)
        source["palette_index"] = index;
    else
        source["note"] = "no ink: ULANext leaves the paper transparent here";
    source["palette"] = int(palette);
    (void)pixel;
    return source;
}

StateNode Inspector::Layer2Layer(Pixel pixel, bool& clipped)
{
    StateNode source = StateNode::Object();
    clipped = false;
    if (!(_segment->flags & Segment::kLayer2Enable))
    {
        source["kind"] = "disabled";
        return source;
    }
    const unsigned resolution = (Nr(0x70) >> 4) & 3;
    const unsigned palette = (_segment->paletteControl & 0x04) ? 5 : 1;
    const unsigned offset = (Nr(0x70) & 0x0F) << 4;
    const unsigned bank = Nr(0x12) & 0x7F;
    const unsigned scrollX = Nr(0x16) | ((Nr(0x71) & 1) << 8);
    const unsigned scrollY = Nr(0x17);
    const unsigned x1 = Window(0, 0), x2 = Window(0, 1), y1 = Window(0, 2), y2 = Window(0, 3);
    static const char* const kResolutions[4] = {"256x192x8", "320x256x8", "640x256x4", "reserved"};
    source["resolution"] = kResolutions[resolution];
    source["bank"] = int(bank);
    if (static_cast<size_t>(bank) * 0x4000 >= static_cast<size_t>(_env.ramPages) * 0x4000)
    {
        source["kind"] = "no_ram";
        source["note"] = "the bank is beyond the installed RAM: nothing is drawn";
        return source;
    }
    long long address = -1;
    int index = -1;
    if (resolution == 0)
    {
        if (_y < kPaperTop || _y >= kPaperTop + 192 || _sub < kPaperLeft || _sub >= kPaperLeft + 512)
        {
            source["kind"] = "outside";
            source["note"] = "the 256x192 mode draws only inside the paper";
            return source;
        }
        const unsigned py = _y - kPaperTop, px = (_sub - kPaperLeft) / 2;
        if (py < y1 || py > y2 || px < x1 || px > x2)
        {
            clipped = true;
            source["kind"] = "clipped";
            return source;
        }
        // layer2.vhd: y(7:6) = y_pre(7:6), + 1 once y_pre reaches 192 (the rows 192-254 follow the 192 rows: not a modulo)
        const unsigned yPre = py + scrollY;
        const unsigned yHigh = ((yPre & 0x100) == 0 && ((yPre >> 6) & 3) != 3) ? ((yPre >> 6) & 3) : (((yPre >> 6) & 3) + 1) & 3;
        const unsigned sy = (yHigh << 6) | (yPre & 0x3F), sx = (px + scrollX) & 255;
        address = static_cast<long long>(sy) * 256 + sx;
    }
    else if (resolution == 1)
    {
        const unsigned px = _sub / 2;
        if (_y < y1 || _y > y2 || px < x1 * 2 || px > x2 * 2 + 1)
        {
            clipped = true;
            source["kind"] = "clipped";
            return source;
        }
        const unsigned sy = (_y + scrollY) & 255, sx = NextVideoRenderer::Layer2WideColumn(px, scrollX);
        address = static_cast<long long>(sx) * 256 + sy;
    }
    else
    {
        if (_y < y1 || _y > y2 || _sub < x1 * 4 || _sub > x2 * 4 + 3)
        {
            clipped = true;
            source["kind"] = "clipped";
            return source;
        }
        const unsigned sy = (_y + scrollY) & 255, sx = NextVideoRenderer::Layer2WideColumn(_sub >> 1, scrollX);
        address = static_cast<long long>(sx) * 256 + sy;
    }
    bool inRam;
    const uint8_t byte = Ram(static_cast<size_t>(bank) * 0x4000 + static_cast<size_t>(address), inRam);
    // the bitmap runs on from the bank (256x192: 3 pages, the others 5): the 16K page the byte is in and its offset in that page
    source["kind"] = "bitmap";
    source["ram_page16"] = int(bank + (static_cast<unsigned>(address) >> 14));
    source["offset"] = Hex(static_cast<unsigned>(address) & 0x3FFF, 4);
    source["bitmap_offset"] = Hex(static_cast<unsigned>(address), address > 0xFFFF ? 5 : 4);
    source["byte"] = Hex(byte, 2);
    if (resolution == 2)
    {
        const unsigned nibble = (_sub & 1) ? (byte & 0x0F) : (byte >> 4);
        source["nibble"] = (_sub & 1) ? "low" : "high";
        index = static_cast<int>(((offset & 0xF0) | nibble) & 0xFF);
    }
    else
        index = static_cast<int>((byte + offset) & 0xFF);
    source["palette_index"] = index;
    source["palette"] = int(palette);
    (void)pixel;
    return source;
}

StateNode Inspector::TilemapLayer(Pixel pixel, bool& clipped)
{
    StateNode source = StateNode::Object();
    clipped = false;
    const uint8_t control = Nr(0x6B);
    if (!(control & 0x80))
    {
        source["kind"] = "disabled";
        return source;
    }
    const unsigned y1 = Window(3, 2), y2 = Window(3, 3);
    const unsigned px320 = _sub >> 1;
    if (_y < y1 || _y > y2 || px320 < Window(3, 0) * 2u || px320 > Window(3, 1) * 2u + 1)
    {
        clipped = true;
        source["kind"] = "clipped";
        return source;
    }
    const bool wide = (control & 0x40) != 0, noFlags = (control & 0x20) != 0, text = (control & 0x08) != 0;
    const bool mode512 = (control & 0x02) != 0, ulaOnTop = (control & 0x01) != 0;
    const unsigned palette = (control & 0x10) ? 7 : 3;
    const unsigned transparentIndex = Nr(0x4C) & 0x0F;
    const unsigned scrollX = Nr(0x30) | ((Nr(0x2F) & 3) << 8);
    const unsigned scrollY = Nr(0x31);
    const unsigned columns = wide ? 80 : 40;
    const unsigned wrap = wide ? 640 : 320;
    const unsigned absY = (_y + scrollY) & 0xFF;
    const unsigned tileRow = absY >> 3, pixelY = absY & 7;
    const unsigned tx = wide ? _sub : px320;
    const unsigned absX = (tx + scrollX) % wrap;
    const unsigned column = absX >> 3, pixelX = absX & 7;
    const unsigned entry = (tileRow * columns + column) * (noFlags ? 1 : 2);
    unsigned tile = Vram(Nr(0x6E), entry);
    const uint8_t attr = noFlags ? Nr(0x6C) : Vram(Nr(0x6E), entry + 1);
    unsigned paletteOffset;
    bool xMirror = false, yMirror = false, rotate = false, ulaOver;
    if (text)
    {
        paletteOffset = (attr >> 1) & 0x7F;
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
    const bool below = ulaOnTop ? false : (ulaOver || mode512);
    unsigned definition, pixelValue;
    if (text)
    {
        definition = tile * 8 + pixelY;
        pixelValue = (Vram(Nr(0x6F), definition) >> (7 - pixelX)) & 1;
    }
    else
    {
        const unsigned px = (xMirror ^ rotate) ? 7 - pixelX : pixelX;
        const unsigned py = yMirror ? 7 - pixelY : pixelY;
        if (!rotate)
        {
            definition = tile * 32 + py * 4 + (px >> 1);
            const uint8_t byte = Vram(Nr(0x6F), definition);
            pixelValue = (px & 1) ? (byte & 0x0F) : (byte >> 4);
        }
        else
        {
            definition = tile * 32 + px * 4 + (py >> 1);
            const uint8_t byte = Vram(Nr(0x6F), definition);
            pixelValue = (py & 1) ? (byte & 0x0F) : (byte >> 4);
        }
    }
    const bool bank7Map = (Nr(0x6E) & 0x80) != 0, bank7Tiles = (Nr(0x6F) & 0x80) != 0;
    _tileBelow = below;
    source["kind"] = text ? "text_tile" : "tile";
    source["column"] = int(column);
    source["row"] = int(tileRow);
    source["map_bank"] = bank7Map ? 7 : 5;
    source["map_offset"] = Hex(((Nr(0x6E) & 0x3F) * 256u + entry) & (bank7Map ? 0x1FFF : 0x3FFF), 4);
    source["tile"] = int(tile);
    source["attribute"] = Hex(attr, 2) + (noFlags ? " (NR #6C, no flags in the map)" : "");
    source["tile_bank"] = bank7Tiles ? 7 : 5;
    source["definition_offset"] = Hex(((Nr(0x6F) & 0x3F) * 256u + definition) & (bank7Tiles ? 0x1FFF : 0x3FFF), 4);
    source["pixel_value"] = int(pixelValue);
    source["x_mirror"] = xMirror;
    source["y_mirror"] = yMirror;
    source["rotate"] = rotate;
    source["below_ula"] = below;
    if (text)
        source["palette_index"] = int(((paletteOffset << 1) | (pixelValue & 1)) & 0xFF);
    else
    {
        source["transparent_index"] = int(transparentIndex);
        source["palette_index"] = int(((paletteOffset << 4) | (pixelValue & 0x0F)) & 0xFF);
    }
    source["palette"] = int(palette);
    (void)pixel;
    return source;
}

StateNode Inspector::SpritesLayer(Pixel pixel, bool& clipped)
{
    StateNode source = StateNode::Object();
    clipped = false;
    if (!(Nr(0x15) & 0x01))
    {
        source["kind"] = "disabled";
        return source;
    }
    // the sprite clip window (DrawLine): over the border, NR #19 as is (x doubled) or the whole grid; under it, in paper coordinates
    const bool overBorder = (Nr(0x15) & 0x02) != 0, borderClip = (Nr(0x15) & 0x20) != 0;
    int clip[4];
    if (overBorder)
    {
        if (!borderClip)
        {
            clip[0] = 0;
            clip[1] = 319;
            clip[2] = 0;
            clip[3] = 255;
        }
        else
        {
            clip[0] = static_cast<int>(Window(1, 0)) * 2;
            clip[1] = static_cast<int>(Window(1, 1)) * 2 + 1;
            clip[2] = static_cast<int>(Window(1, 2));
            clip[3] = static_cast<int>(Window(1, 3));
        }
    }
    else
    {
        auto shifted = [](unsigned v) { return static_cast<int>(((((v >> 5) & 7) + 1) << 5) | (v & 0x1F)); };
        for (unsigned c = 0; c < 4; c++)
            clip[c] = shifted(Window(1, c));
    }
    const int gx = static_cast<int>(_x), gy = static_cast<int>(_y);
    const bool outsideClip = gx < clip[0] || gx > clip[1] || gy < clip[2] || gy > clip[3] || (!overBorder && _y >= 224);
    if (!_env.sprites)
    {
        source["kind"] = "unknown";
        return source;
    }
    NextSprites::Info info[NextSprites::kSprites];
    _env.sprites->Describe(info);
    // the sprites whose box covers the pixel (the engine's own geometry: 16 << scale pixels, wrapping at 512)
    int firstCovering = -1;
    int transparentCovering = -1;
    int number = -1;
    unsigned patternOffset = 0;
    uint8_t patternByte = 0;
    bool fourBit = false;
    bool patternHigh = false;  // 4-bit: the pixel is the high nibble of the byte
    for (unsigned i = 0; i < NextSprites::kSprites; i++)
    {
        const NextSprites::Info& s = info[i];
        if (!s.visible)
            continue;
        const int height = 16 * static_cast<int>(s.yScale), width = 16 * static_cast<int>(s.xScale);
        int dy = gy - s.y;
        if (dy < 0)
            dy += 512;
        int dx = (gx - s.x) & 0x1FF;
        if (dy >= height || dx >= width)
            continue;
        if (firstCovering < 0)
            firstCovering = static_cast<int>(i);
        const unsigned xShift = s.xScale == 8 ? 3 : (s.xScale == 4 ? 2 : (s.xScale == 2 ? 1 : 0));
        const unsigned yShift = s.yScale == 8 ? 3 : (s.yScale == 4 ? 2 : (s.yScale == 2 ? 1 : 0));
        const int row = dy >> yShift;
        const int rowIndex = s.yMirror ? 15 - row : row;
        const bool columnMirror = s.xMirror ^ s.rotate;
        const int patternCol = dx >> xShift;
        int px = columnMirror ? 15 - patternCol : patternCol;
        int py = rowIndex;
        if (s.rotate)
            std::swap(px, py);
        unsigned offset;
        uint8_t raw;
        bool transparentPixel;
        const uint8_t transparentIndex = Nr(0x4B);
        if (!s.fourBit)
        {
            offset = ((s.pattern << 8) | (static_cast<unsigned>(py) << 4) | static_cast<unsigned>(px)) & (NextSprites::kPatternBytes - 1);
            raw = _env.sprites->PatternByte(offset);
            transparentPixel = raw == transparentIndex;
        }
        else
        {
            offset = (((s.pattern >> 1) << 8) | ((s.pattern & 1) << 7) | (static_cast<unsigned>(py) << 3) | (static_cast<unsigned>(px) >> 1)) &
                     (NextSprites::kPatternBytes - 1);
            raw = _env.sprites->PatternByte(offset);
            const unsigned nibble = (px & 1) ? (raw & 0x0F) : (raw >> 4);
            transparentPixel = nibble == (transparentIndex & 0x0F);
        }
        if (transparentPixel)
        {
            if (transparentCovering < 0)
                transparentCovering = static_cast<int>(i);
            continue;
        }
        // the winner among the painting sprites: the engine draws 0 first and lets later ones overwrite, or the reverse with NR #15 bit 6
        const bool zeroOnTop = (Nr(0x15) & 0x40) != 0;
        if (number < 0 || !zeroOnTop)
        {
            number = static_cast<int>(i);
            patternOffset = offset;
            patternByte = raw;
            fourBit = s.fourBit;
            patternHigh = !(px & 1);
        }
    }
    // the record says who painted: its sprite number is authoritative, the geometry above only explains
    if (pixel.opaque)
    {
        const int recorded = _frame.info[_at];
        const NextSprites::Info& s = info[recorded & 127];
        source["kind"] = "sprite";
        source["sprite"] = recorded;
        source["x"] = s.x;
        source["y"] = s.y;
        source["pattern"] = int(s.pattern);
        source["four_bit"] = s.fourBit;
        if (number == recorded)
        {
            source["pattern_offset"] = Hex(patternOffset, 4);
            source["pattern_byte"] = Hex(patternByte, 2);
            const unsigned palette = (_segment->paletteControl & 0x08) ? 6 : 2;
            source["palette"] = int(palette);
            if (!fourBit)
                source["palette_index"] = int((((patternByte >> 4) + s.palette) << 4 | (patternByte & 0x0F)) & 0xFF);
            else
                source["palette_index"] = int(((s.palette << 4) | (patternHigh ? (patternByte >> 4) : (patternByte & 0x0F))) & 0xFF);
        }
        else
            source["note"] = "the sprite memory changed since the frame was drawn: the pattern byte is not recomputed";
        return source;
    }
    if (outsideClip)
    {
        clipped = true;
        source["kind"] = "clipped";
        return source;
    }
    if (firstCovering < 0)
    {
        source["kind"] = "none";
        source["note"] = "no visible sprite covers this pixel";
        return source;
    }
    source["kind"] = "transparent";
    source["sprite"] = transparentCovering >= 0 ? transparentCovering : firstCovering;
    source["note"] = "the sprite's pattern byte here is the sprite transparency index (NR #4B)";
    return source;
}

/// Why a layer that painted did not win, in words
std::string Inspector::LostBecause(unsigned layer, unsigned winner, Pixel pixel) const
{
    const unsigned order = (Nr(0x15) >> 2) & 7;
    static const char* const kLayerNames[4] = {"ula", "layer2", "tilemap", "sprites"};
    if (winner == kIdStencil && (layer == kUla || layer == kTilemap))
        return "ANDed with the " + std::string(layer == kUla ? "tilemap" : "ula") + " pixel (stencil mode, NR #68 bit 0)";
    if (winner == kIdBlend)
        return "mixed into the blend result (layer order " + std::string(kOrders[order]) + ", mix layer NR #68 bits 6:5)";
    if (winner == kIdLayer2 && layer != kLayer2 && (_frame.plane[kLayer2][_at].colour & 0x200))
        return "Layer 2 priority colour is above everything";
    if (layer == kTilemap && (winner == kIdUla || winner == kIdBorder) && pixel.below)
        return "below the ULA (tile attribute bit 0, or 512 mode)";
    if (layer == kUla && winner == kIdTilemap)
        return "covered by the tilemap pixel (the tile is above the ULA)";
    if (layer == kUla && winner == kIdSprite && order >= 3 && order <= 5 && _frame.plane[kUla][_at].border)
        return std::string("layer order ") + kOrders[order] + ": the ULA border does not hide a sprite";
    return std::string("layer order ") + kOrders[order] + ": " + IdName(winner) + " above " + kLayerNames[layer];
}

StateNode Inspector::Run()
{
    StateNode n = StateNode::Object();
    n["available"] = true;
    n["ok"] = true;
    StateNode frame = StateNode::Object();
    frame["sequence"] = static_cast<uint64_t>(_frame.sequence);
    frame["frame_number"] = static_cast<uint64_t>(_frame.frameCounter);
    n["frame"] = std::move(frame);
    StateNode where = StateNode::Object();
    where["x"] = int(_x);
    where["y"] = int(_y);
    where["subpixel"] = int(_sub & 1);
    where["subpixel_x"] = int(_sub);
    where["segment_from_x"] = int(_segment->fromX);
    where["segment_to_x"] = int(_segment->toX);
    n["pixel"] = std::move(where);

    const unsigned winner = _frame.ids[_at];
    const uint16_t composed = ComposedColour(_frame, _y, _sub);
    StateNode win = StateNode::Object();
    win["layer"] = IdName(winner);
    win["id"] = int(winner);
    n["winner"] = std::move(win);
    StateNode colour = StateNode::Object();
    colour["rgb9"] = Hex(composed, 3);
    colour["rgb"] = Rgb(composed);
    n["colour"] = std::move(colour);
    const unsigned order = (Nr(0x15) >> 2) & 7;
    n["layer_order"] = kOrders[order];

    static const char* const kNames[4] = {"ula", "layer2", "tilemap", "sprites"};
    StateNode layers = StateNode::Object();
    for (unsigned l = 0; l < kPlanes; l++)
    {
        const Pixel pixel = _frame.plane[l][_at];
        bool clipped = false;
        StateNode source;
        switch (l)
        {
            case kUla: source = UlaLayer(pixel, clipped); break;
            case kLayer2: source = Layer2Layer(pixel, clipped); break;
            case kTilemap: source = TilemapLayer(pixel, clipped); break;
            default: source = SpritesLayer(pixel, clipped); break;
        }
        StateNode layer = StateNode::Object();
        layer["opaque"] = pixel.opaque != 0;
        layer["rgb9"] = Hex(pixel.colour & 0x1FF, 3);
        if (pixel.opaque)
            layer["rgb"] = Rgb(pixel.colour);
        const bool wins = (l == kUla && (winner == kIdUla || winner == kIdBorder)) || (l == kLayer2 && winner == kIdLayer2) ||
                          (l == kTilemap && winner == kIdTilemap) || (l == kSprites && winner == kIdSprite);
        layer["wins"] = wins;
        std::string why;
        const std::string kind = source.find("kind") ? source.find("kind")->s : std::string();
        if (wins)
            why = "wins (layer order " + std::string(kOrders[order]) + ")";
        else if (pixel.opaque)
        {
            // the border exception: a border pixel does not hide a sprite in the orders LUS / USL / ULS
            if (l == kUla && winner == kIdSprite && order >= 3 && order <= 5 && pixel.border)
                why = std::string("layer order ") + kOrders[order] + ": the ULA border does not hide a sprite";
            else
                why = LostBecause(l, winner, pixel);
        }
        else if (kind == "disabled")
        {
            static const char* const kOff[4] = {"ULA disabled (NR #68 bit 7)", "Layer 2 disabled (port #123B bit 1 / NR #69 bit 7)",
                                                "tilemap disabled (NR #6B bit 7)", "sprites disabled (NR #15 bit 0)"};
            why = kOff[l];
        }
        else if (clipped || kind == "clipped")
        {
            static const char* const kClip[4] = {"clipped (ULA clip window NR #1A)", "clipped (Layer 2 clip window NR #18)",
                                                 "clipped (tilemap clip window NR #1B)", "clipped (sprite clip window NR #19)"};
            why = kClip[l];
        }
        else if (kind == "outside")
            why = "outside the area this layer draws";
        else if (kind == "no_ram")
            why = "the bank is beyond the installed RAM";
        else if (kind == "none")
            why = "no sprite here";
        else if (l == kSprites && kind == "transparent")
            why = "transparent (sprite pattern byte equals the sprite transparency index NR #4B)";
        else if (l == kTilemap && source.find("transparent_index") && source.find("pixel_value") &&
                 source.find("pixel_value")->i == source.find("transparent_index")->i)
            why = "transparent (tile pixel index " + std::to_string(source.find("pixel_value")->i) + " is the tilemap transparent index NR #4C)";
        else if (l == kUla && kind == "border" && source.find("note") && source.find("note")->s.find("full-ink") != std::string::npos)
            why = "transparent (ULANext full-ink mode: the border comes from the fallback colour)";
        else if (source.find("note") && source.find("note")->s.find("no ink") != std::string::npos)
            why = "transparent (ULANext: no ink here)";
        else
        {
            // painted in the global transparency colour
            const unsigned nr14 = Nr(0x14);
            uint16_t index9 = 0;
            const StateNode* palette = source.find("palette");
            const StateNode* idx = source.find("palette_index");
            if (palette && idx && _env.regs)
                index9 = PaletteColour(static_cast<unsigned>(palette->i), static_cast<unsigned>(idx->i));
            if (palette && idx && ((index9 >> 1) & 0xFF) == nr14)
                why = "transparent (colour " + Hex(index9, 3) + " is the global transparency colour NR #14)";
            else if (l == kTilemap && source.find("transparent_index"))
                why = "transparent (tile text pixel is the global transparency colour NR #14)";
            else
                why = "transparent";
        }
        layer["why"] = why;
        layer["source"] = std::move(source);
        layers[kNames[l]] = std::move(layer);
    }
    n["layers"] = std::move(layers);
    return n;
}
}  // namespace

uint16_t ComposedColour(const Frame& frame, unsigned y, unsigned subpixel)
{
    const size_t at = static_cast<size_t>(y) * kWidth + subpixel;
    const Segment* segment = frame.SegmentAt(y, subpixel);
    const uint8_t id = frame.ids[at];
    const Pixel ula = frame.plane[kUla][at], l2 = frame.plane[kLayer2][at], tile = frame.plane[kTilemap][at], sprite = frame.plane[kSprites][at];
    switch (id)
    {
        case kIdUla:
        case kIdBorder: return ula.colour & 0x1FF;
        case kIdLayer2: return l2.colour & 0x1FF;
        case kIdTilemap: return tile.colour & 0x1FF;
        case kIdSprite: return sprite.colour & 0x1FF;
        case kIdStencil: return Pack(R3(ula.colour) & R3(tile.colour), G3(ula.colour) & G3(tile.colour), B2(ula.colour) & B2(tile.colour));
        case kIdBlend:
        {
            const unsigned order = segment ? (segment->Reg(0x15) >> 2) & 7 : 6;
            const unsigned blend = segment ? (segment->Reg(0x68) >> 5) & 3 : 0;
            const bool stencilMode = segment && (segment->Reg(0x68) & 0x01) && (segment->Reg(0x6B) & 0x80) && !(segment->Reg(0x68) & 0x80);
            const bool lv = l2.opaque, ulaV = ula.opaque, tv = tile.opaque, below = tile.below;
            bool mixOpaque = false;
            uint16_t mix = 0;
            switch (blend)
            {
                case 0: mixOpaque = ulaV; mix = ula.colour; break;
                case 1: break;
                case 2:
                    if (stencilMode)
                    {
                        mixOpaque = ulaV && tv;
                        mix = Pack(R3(ula.colour) & R3(tile.colour), G3(ula.colour) & G3(tile.colour), B2(ula.colour) & B2(tile.colour));
                    }
                    else if (tv && (!below || !ulaV))
                    {
                        mixOpaque = true;
                        mix = tile.colour;
                    }
                    else
                    {
                        mixOpaque = ulaV;
                        mix = ula.colour;
                    }
                    break;
                default: mixOpaque = tv; mix = tile.colour; break;
            }
            unsigned r = (lv ? R3(l2.colour) : 0) + (mixOpaque ? R3(mix) : 0);
            unsigned g = (lv ? G3(l2.colour) : 0) + (mixOpaque ? G3(mix) : 0);
            unsigned b = (lv ? B2(l2.colour) : 0) + (mixOpaque ? B2(mix) : 0);
            if (order == 6)
                return Pack(std::min(r, 7u), std::min(g, 7u), std::min(b, 3u));
            auto sub = [](unsigned v) -> unsigned { return v <= 4 ? 0 : (((v >> 2) & 3) == 3 ? 7 : ((v + 0x0B) & 0x0F)); };
            if (mixOpaque)
            {
                r = sub(r);
                g = sub(g);
                b = sub(b);
            }
            return Pack(r & 7, g & 7, b & 3);
        }
        default: return Nine(segment ? segment->Reg(0x4A) : 0);
    }
}

StateNode InspectPixel(const Frame& frame, const InspectEnv& env, unsigned x, unsigned y, unsigned sx)
{
    Inspector inspector(frame, env, x, y, sx & 1);
    if (x >= kWidth / 2 || y >= kHeight)
    {
        StateNode n = StateNode::Object();
        n["available"] = true;
        n["ok"] = false;
        n["status"] = "bad-parameter";
        n["message"] = "x is 0-319 and y 0-255";
        return n;
    }
    if (!inspector.HasSegment())
    {
        StateNode n = StateNode::Object();
        n["available"] = true;
        n["ok"] = false;
        n["status"] = "no-frame";
        n["message"] = "the frame has no record of this line";
        return n;
    }
    return inspector.Run();
}
}  // namespace NextLayer
