#include "profivideomapper.h"

#include "emulator/video/profi/profigeometry.h"

using namespace videomap;
using namespace ProfiGeometry;

namespace
{
SourceRef PaletteCell(uint8_t index)
{
    return {Space::Palette, 0, 0, index, 2, 0x1FF, SourceRole::PaletteEntry};
}

uint32_t PaletteRgb(const VideoState& s, uint8_t index)
{
    return s.profiPalette ? PaletteColour(s.profiPalette[index & 0x0F]) : 0xFF000000u;
}
} // namespace

VideoLayout ProfiVideoMapper::Layout(const VideoState& s) const
{
    const RasterDescriptor& l = s.layoutDesc;
    VideoLayout layout;
    layout.mapped = true;
    layout.family = Family();
    layout.mode = s.mode;
    layout.tstatesPerLine = kTStatesPerLine;
    layout.lines = static_cast<uint16_t>(kVSyncVBlankLines + kVisibleLines);

    LayerDesc layer;
    layer.id = "profihr";
    layer.surface = {static_cast<uint16_t>(kWidth), static_cast<uint16_t>(kScreenLines), 1};
    layer.window.firstLine = static_cast<uint16_t>(kVSyncVBlankLines + l.screenOffsetTop);
    layer.window.lineCount = static_cast<uint16_t>(kScreenLines);
    layer.window.firstT = static_cast<uint16_t>(kPaperStartT);
    layer.window.tCount = static_cast<uint16_t>(kPaperTStates);
    layer.window.dotsPerT = 4;
    layout.layers.push_back(layer);

    layout.fb = {l.fullFrameWidth, l.fullFrameHeight, l.screenOffsetLeft, l.screenOffsetTop};
    return layout;
}

bool ProfiVideoMapper::SourcesAt(const VideoState& s, const MemView& m, size_t layerIndex, uint32_t x, uint32_t y,
                                 LayerContribution& out) const
{
    if (layerIndex != 0 || x >= kWidth || y >= kScreenLines)
        return false;

    const uint32_t byteIndex = x / 8;
    const uint16_t offset = ByteOffset(y, byteIndex);
    const uint16_t bit = static_cast<uint16_t>(0x80 >> (x % 8));
    const SourceRef pixel{Space::Ram, 0, PixelPage(s.p7FFD), offset, 1, bit, SourceRole::PixelBits};
    const bool set = (m.Read(pixel) & bit) != 0;

    out.layer = "profihr";
    if (s.profiMonochrome)
    {
        out.colourIndex = AttrColourIndex(MonochromeAttr(s.pFE), set);
        out.sources = {pixel, {Space::Register, 0, 0xFE, 0, 1, 0x07, SourceRole::Attribute}, PaletteCell(out.colourIndex)};
    }
    else
    {
        SourceRef attr{Space::Ram, 0, AttrPage(s.p7FFD, s.ramMask), offset, 1, 0xFF, SourceRole::Attribute};
        out.colourIndex = AttrColourIndex(m.Read(attr), set);
        attr.bitMask = static_cast<uint16_t>(set ? 0x47 : 0xB8);
        out.sources = {pixel, attr, PaletteCell(out.colourIndex)};
    }
    out.rgb = PaletteRgb(s, out.colourIndex);
    return true;
}

void ProfiVideoMapper::BorderSources(const VideoState& s, LayerContribution& out) const
{
    // The border shows through the palette with the inverted index
    out.layer = "border";
    out.colourIndex = static_cast<uint8_t>(~s.borderIndex & 0x07);
    out.sources = {{Space::Register, 0, 0xFE, 0, 1, 0x07, SourceRole::Border}, PaletteCell(out.colourIndex)};
    out.rgb = PaletteRgb(s, out.colourIndex);
}

void ProfiVideoMapper::PixelsFor(const VideoState& s, const SourceRef& ref, std::vector<SurfaceArea>& out) const
{
    if (ref.space != Space::Ram || ref.offset >= 0x4000)
        return;
    const bool pixelPage = ref.page == PixelPage(s.p7FFD);
    const bool attrPage = !s.profiMonochrome && ref.page == AttrPage(s.p7FFD, s.ramMask);
    if (!pixelPage && !attrPage)
        return;
    uint32_t v = 0, byteIndex = 0;
    if (DecodeByteOffset(static_cast<uint16_t>(ref.offset), v, byteIndex))
        out.push_back({"profihr", static_cast<uint16_t>(8 * byteIndex), static_cast<uint16_t>(v), 8, 1});
}
