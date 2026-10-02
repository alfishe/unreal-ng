#include "atmvideomapper.h"

#include "emulator/video/atm/atmfont.h"
#include "emulator/video/atm/atmgeometry.h"
#include "emulator/video/zx/zxgeometry.h"

using namespace videomap;
using namespace AtmGeometry;

namespace
{
SourceRef PaletteCell(uint8_t index)
{
    return {Space::Palette, 0, 0, index, 1, 0xFF, SourceRole::PaletteEntry};
}

uint32_t PaletteRgb(const VideoState& s, uint8_t index)
{
    return s.atmPalette ? s.atmPalette[index & 0x0F] : ZxGeometry::Colour(index);
}

const char* LayerId(VideoModeEnum mode)
{
    switch (mode)
    {
        case M_ATM16: return "atm16";
        case M_ATMHR: return "atmhr";
        case M_ATMTX: return "atmtx";
        default:      return "atmtl";
    }
}

bool IsText(VideoModeEnum mode)
{
    return mode == M_ATMTX || mode == M_ATMTL;
}
} // namespace

VideoLayout AtmVideoMapper::Layout(const VideoState& s) const
{
    const RasterDescriptor& l = s.layoutDesc;
    VideoLayout layout;
    layout.mapped = true;
    layout.family = Family();
    layout.mode = s.mode;
    layout.tstatesPerLine = kTStatesPerLine;
    // Lines before the visible area from the timing descriptor: 24 on ATM 7.10 / ATM3, 20 on ATM450
    // (docs/inprogress/2026-10-01-atm450/frame-timing-protection.md)
    const uint16_t timingBlank = static_cast<uint16_t>(s.timingDesc.vSyncLines + s.timingDesc.vBlankLines);
    const uint16_t blankLines = timingBlank ? timingBlank : static_cast<uint16_t>(kVSyncVBlankLines);
    layout.lines = static_cast<uint16_t>(blankLines + kVisibleLines);

    LayerDesc layer;
    layer.id = LayerId(s.mode);
    const bool wide = s.mode != M_ATM16;
    layer.surface = {static_cast<uint16_t>(wide ? 640 : 320), static_cast<uint16_t>(kScreenLines),
                     static_cast<uint8_t>(s.mode == M_ATM16 ? 4 : 1)};
    if (IsText(s.mode))
    {
        layer.surface.textColumns = kTextColumns;
        layer.surface.textRows = kTextRows;
    }
    layer.window.firstLine = static_cast<uint16_t>(blankLines + l.screenOffsetTop);
    layer.window.lineCount = static_cast<uint16_t>(kScreenLines);
    layer.window.firstT = static_cast<uint16_t>(kScreenStartT);
    layer.window.tCount = static_cast<uint16_t>(kScreenEndT - kScreenStartT);
    layer.window.dotsPerT = static_cast<uint8_t>(wide ? 4 : 2);
    layout.layers.push_back(layer);

    layout.fb = {l.fullFrameWidth, l.fullFrameHeight, l.screenOffsetLeft, l.screenOffsetTop};
    return layout;
}

void AtmVideoMapper::TextCellSources(const VideoState& s, uint32_t n, uint32_t r, SourceRef& code, SourceRef& attr)
{
    const uint8_t videoPage = VideoPage(s.p7FFD);
    const bool even = (n % 2) == 0;
    if (s.mode == M_ATMTL)
    {
        const uint16_t page = TextLinearPage(videoPage);
        const uint32_t rowBase = r * kTextRowStride;
        code = {Space::Ram, 0, page, (even ? kTlCodeEven : kTlCodeOdd) + rowBase + (n >> 1), 1, 0xFF,
                SourceRole::CharCode};
        attr = {Space::Ram, 0, page, (even ? kTlAttrEven : kTlAttrOdd) + rowBase + ((n + 1) >> 1), 1, 0xFF,
                SourceRole::CharAttr};
        return;
    }
    const uint32_t byteIdx = kTextBase + kTextRowStride * r + n / 2;
    code = {Space::Ram, 0, videoPage, even ? byteIdx : kPlaneHigh + byteIdx, 1, 0xFF, SourceRole::CharCode};
    attr = {Space::Ram, 0, AltPage(videoPage), even ? kPlaneHigh + byteIdx : 1 + byteIdx, 1, 0xFF, SourceRole::CharAttr};
}

bool AtmVideoMapper::SourcesAt(const VideoState& s, const MemView& m, size_t layerIndex, uint32_t x, uint32_t y,
                               LayerContribution& out) const
{
    const uint32_t width = s.mode == M_ATM16 ? 320 : 640;
    if (layerIndex != 0 || x >= width || y >= kScreenLines)
        return false;

    const uint8_t videoPage = VideoPage(s.p7FFD);
    const uint8_t altPage = AltPage(videoPage);
    const uint32_t lineBase = y * kBytesPerLine;
    out.layer = LayerId(s.mode);

    if (s.mode == M_ATM16)
    {
        const uint32_t j = x / 8;
        const uint32_t q = (x / 2) & 3;
        const bool right = (x & 1) != 0;
        const SourceRef plane{Space::Ram, 0, static_cast<uint16_t>((q & 1) ? videoPage : altPage),
                              ((q >> 1) << 13) + lineBase + j, 1, static_cast<uint16_t>(right ? 0xB8 : 0x47),
                              SourceRole::Plane};
        out.colourIndex = PairColourIndex(m.Read(plane), right);
        out.sources = {plane, PaletteCell(out.colourIndex)};
        out.rgb = PaletteRgb(s, out.colourIndex);
        return true;
    }

    const uint16_t bit = static_cast<uint16_t>(0x80 >> (x % 8));
    if (s.mode == M_ATMHR)
    {
        const uint32_t n = x / 8;  // pixel byte group 0..79
        const uint32_t plane = (n % 2) ? kPlaneHigh : 0;
        const SourceRef pixel{Space::Ram, 0, videoPage, plane + lineBase + n / 2, 1, bit, SourceRole::PixelBits};
        const SourceRef attrAll{Space::Ram, 0, altPage, plane + lineBase + n / 2, 1, 0xFF, SourceRole::Attribute};
        const bool set = (m.Read(pixel) & bit) != 0;
        out.colourIndex = AttrColourIndex(m.Read(attrAll), set);
        SourceRef attr = attrAll;
        attr.bitMask = static_cast<uint16_t>(set ? 0x47 : 0xB8);
        out.sources = {pixel, attr, PaletteCell(out.colourIndex)};
        out.rgb = PaletteRgb(s, out.colourIndex);
        return true;
    }

    // Text modes: char code + attribute + font row
    SourceRef code, attr;
    TextCellSources(s, x / 8, y / 8, code, attr);
    const uint8_t charCode = m.Read(code);
    const SourceRef font{Space::InternalTable, 0, 0, static_cast<uint32_t>((y % 8) * 256 + charCode), 1, bit,
                         SourceRole::FontRow};
    const bool set = (m.Read(font) & bit) != 0;
    out.colourIndex = AttrColourIndex(m.Read(attr), set);
    attr.bitMask = static_cast<uint16_t>(set ? 0x47 : 0xB8);
    out.sources = {code, attr, font, PaletteCell(out.colourIndex)};
    out.rgb = PaletteRgb(s, out.colourIndex);
    return true;
}

void AtmVideoMapper::BorderSources(const VideoState& s, LayerContribution& out) const
{
    out.layer = "border";
    out.colourIndex = static_cast<uint8_t>((s.borderAttr & 0x07) | (s.atmBorderBright ? 0x08 : 0x00));
    out.sources = {{Space::Register, 0, 0xFE, 0, 1, 0x07, SourceRole::Border}, PaletteCell(out.colourIndex)};
    out.rgb = PaletteRgb(s, out.colourIndex);
}

void AtmVideoMapper::PixelsFor(const VideoState& s, const SourceRef& ref, std::vector<SurfaceArea>& out) const
{
    if (ref.space != Space::Ram || ref.offset >= 0x4000)
        return;
    const uint8_t videoPage = VideoPage(s.p7FFD);
    const uint8_t altPage = AltPage(videoPage);
    const char* id = LayerId(s.mode);

    if (s.mode == M_ATM16 || s.mode == M_ATMHR)
    {
        if (ref.page != videoPage && ref.page != altPage)
            return;
        const uint32_t hi = ref.offset >> 13;
        const uint32_t base = ref.offset & 0x1FFF;
        const uint32_t y = base / kBytesPerLine;
        const uint32_t j = base % kBytesPerLine;
        if (y >= kScreenLines)
            return;
        if (s.mode == M_ATM16)
        {
            const uint32_t q = (ref.page == videoPage ? 1u : 0u) | (hi << 1);
            out.push_back({id, static_cast<uint16_t>(8 * j + 2 * q), static_cast<uint16_t>(y), 2, 1});
        }
        else
        {
            out.push_back({id, static_cast<uint16_t>(8 * (2 * j + hi)), static_cast<uint16_t>(y), 8, 1});
        }
        return;
    }

    // Text: scan the grid (80x25) for the cells this byte feeds
    for (uint32_t r = 0; r < kTextRows; ++r)
    {
        for (uint32_t n = 0; n < kTextColumns; ++n)
        {
            SourceRef code, attr;
            TextCellSources(s, n, r, code, attr);
            if ((code.page == ref.page && code.offset == ref.offset) || (attr.page == ref.page && attr.offset == ref.offset))
                out.push_back({id, static_cast<uint16_t>(8 * n), static_cast<uint16_t>(8 * r), 8, 8});
        }
    }
}

bool AtmVideoMapper::TextAt(const VideoState& s, const MemView& m, size_t layerIndex, uint32_t col, uint32_t row,
                            TextCell& out) const
{
    if (layerIndex != 0 || !IsText(s.mode) || col >= kTextColumns || row >= kTextRows)
        return false;
    TextCellSources(s, col, row, out.codeSource, out.attrSource);
    out.code = m.Read(out.codeSource);
    out.attr = m.Read(out.attrSource);
    return true;
}
