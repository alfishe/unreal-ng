#include "zxvideomapper.h"

#include "emulator/video/zx/zxgeometry.h"

using namespace videomap;

VideoLayout ZxVideoMapper::Layout(const VideoState& s) const
{
    const RasterDescriptor& t = s.timingDesc;
    const RasterDescriptor& l = s.layoutDesc;
    VideoLayout layout;
    layout.mapped = true;
    layout.family = Family();
    layout.mode = s.mode;
    layout.tstatesPerLine = static_cast<uint16_t>(t.pixelsPerLine / 2);
    layout.lines = static_cast<uint16_t>(t.vSyncLines + t.vBlankLines + t.fullFrameHeight);

    LayerDesc zx;
    zx.id = "zx";
    zx.surface = {ZxGeometry::kWidth, ZxGeometry::kHeight, 1};
    zx.window.firstLine = static_cast<uint16_t>(t.vSyncLines + t.vBlankLines + t.screenOffsetTop);
    zx.window.lineCount = ZxGeometry::kHeight;
    zx.window.firstT = static_cast<uint16_t>(t.screenOffsetLeft / 2);
    zx.window.tCount = ZxGeometry::kWidth / 2;
    zx.window.dotsPerT = 2;
    layout.layers.push_back(zx);

    // The storage row says where the paper sits in the buffer (P384: 64, its 16 extra lines are on top)
    layout.fb = {l.fullFrameWidth, l.fullFrameHeight, l.screenOffsetLeft, l.screenOffsetTop};
    return layout;
}

bool ZxVideoMapper::SourcesAt(const VideoState& s, const MemView& m, size_t layerIndex, uint32_t x, uint32_t y,
                              LayerContribution& out) const
{
    if (layerIndex != 0 || x >= ZxGeometry::kWidth || y >= ZxGeometry::kHeight)
        return false;

    const uint8_t col = static_cast<uint8_t>(x / 8);
    const uint16_t bit = static_cast<uint16_t>(0x80 >> (x % 8));
    const SourceRef pixel{Space::Ram, 0, s.zxScreenPage, ZxGeometry::PixelOffset(static_cast<uint8_t>(y), col), 1, bit,
                          SourceRole::PixelBits};
    const uint16_t attrOffset = ZxGeometry::AttrOffset(static_cast<uint8_t>(y), col);
    const uint8_t attr = m.Read({Space::Ram, 0, s.zxScreenPage, attrOffset, 1, 0xFF, SourceRole::Attribute});
    const bool set = (m.Read(pixel) & bit) != 0;

    out.layer = "zx";
    out.sources = {pixel, {Space::Ram, 0, s.zxScreenPage, attrOffset, 1, static_cast<uint16_t>(set ? 0x47 : 0x78),
                           SourceRole::Attribute}};
    out.colourIndex = ZxGeometry::AttrColourIndex(attr, set);
    out.rgb = ZxGeometry::Colour(out.colourIndex);
    return true;
}

void ZxVideoMapper::BorderSources(const VideoState& s, LayerContribution& out) const
{
    out.layer = "border";
    out.sources = {{Space::Register, 0, 0xFE, 0, 1, 0x07, SourceRole::Border}};
    out.colourIndex = static_cast<uint8_t>(s.borderIndex & 0x07);
    out.rgb = ZxGeometry::Colour(out.colourIndex);
}

void ZxVideoMapper::PixelsFor(const VideoState& s, const SourceRef& ref, std::vector<SurfaceArea>& out) const
{
    if (ref.space != Space::Ram || ref.page != s.zxScreenPage)
        return;
    if (ref.offset < ZxGeometry::kPixelBytes)
    {
        uint8_t y = 0, col = 0;
        ZxGeometry::DecodePixelOffset(static_cast<uint16_t>(ref.offset), y, col);
        out.push_back({"zx", static_cast<uint16_t>(col * 8), y, 8, 1});
    }
    else if (ref.offset < ZxGeometry::kAttrBase + ZxGeometry::kAttrBytes)
    {
        const uint32_t cell = ref.offset - ZxGeometry::kAttrBase;
        out.push_back({"zx", static_cast<uint16_t>((cell % 32) * 8), static_cast<uint16_t>((cell / 32) * 8), 8, 8});
    }
}
