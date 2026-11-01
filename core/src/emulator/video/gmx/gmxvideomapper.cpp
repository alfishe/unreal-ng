#include "gmxvideomapper.h"

#include "emulator/video/gmx/gmxgeometry.h"
#include "emulator/video/zx/zxgeometry.h"

using namespace videomap;
using namespace GmxGeometry;

VideoLayout GmxVideoMapper::Layout(const VideoState& s) const
{
    const RasterDescriptor& l = s.layoutDesc;
    VideoLayout layout;
    layout.mapped = true;
    layout.family = Family();
    layout.mode = s.mode;
    layout.tstatesPerLine = kTStatesPerLine;
    layout.lines = static_cast<uint16_t>(kVSyncVBlankLines + kVisibleLines);

    LayerDesc layer;
    layer.id = "gmx";
    layer.surface = {static_cast<uint16_t>(kWidth), static_cast<uint16_t>(kScreenLines), 1};
    layer.window.firstLine = static_cast<uint16_t>(kVSyncVBlankLines + l.screenOffsetTop);
    layer.window.lineCount = static_cast<uint16_t>(kScreenLines);
    layer.window.firstT = static_cast<uint16_t>(kPaperStartT);
    layer.window.tCount = static_cast<uint16_t>(kPaperTStates);
    layer.window.dotsPerT = kPixelsPerT;
    layout.layers.push_back(layer);

    layout.fb = {l.fullFrameWidth, l.fullFrameHeight, l.screenOffsetLeft, l.screenOffsetTop};
    return layout;
}

bool GmxVideoMapper::SourcesAt(const VideoState& s, const MemView& m, size_t layerIndex, uint32_t x, uint32_t y,
                               LayerContribution& out) const
{
    if (layerIndex != 0 || x >= kWidth || y >= kScreenLines)
        return false;

    const uint16_t offset = ByteOffset(y, x / 8, s.gmxScrollStart);
    const uint16_t bit = static_cast<uint16_t>(0x80 >> (x % 8));
    const SourceRef pixel{Space::Ram, 0, PixelPage(s.p7FFD), offset, 1, bit, SourceRole::PixelBits};
    SourceRef attr{Space::Ram, 0, AttrPage(s.p7FFD), offset, 1, 0xFF, SourceRole::Attribute};
    const uint8_t attrByte = m.Read(attr);
    const bool ink = Ink(m.Read(pixel), x % 8, attrByte, s.flashPhase);

    out.layer = "gmx";
    out.colourIndex = AttrColourIndex(attrByte, ink);
    attr.bitMask = static_cast<uint16_t>(ink ? 0xC7 : 0xF8);
    out.sources = {pixel, attr};
    out.rgb = ZxGeometry::Colour(out.colourIndex);
    return true;
}

void GmxVideoMapper::BorderSources(const VideoState& s, LayerContribution& out) const
{
    out.layer = "border";
    out.colourIndex = static_cast<uint8_t>(s.borderIndex & 0x07);
    out.sources = {{Space::Register, 0, 0xFE, 0, 1, 0x07, SourceRole::Border}};
    out.rgb = ZxGeometry::Colour(out.colourIndex);
}

void GmxVideoMapper::PixelsFor(const VideoState& s, const SourceRef& ref, std::vector<SurfaceArea>& out) const
{
    if (ref.space != Space::Ram || ref.offset >= kScreenBytes)
        return;
    if (ref.page != PixelPage(s.p7FFD) && ref.page != AttrPage(s.p7FFD))
        return;
    uint32_t y = 0, column = 0;
    if (DecodeByteOffset(static_cast<uint16_t>(ref.offset), s.gmxScrollStart, y, column))
        out.push_back({"gmx", static_cast<uint16_t>(8 * column), static_cast<uint16_t>(y), 8, 1});
}
