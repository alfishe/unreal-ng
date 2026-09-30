#include "alcovideomapper.h"

#include "emulator/video/atm/atmgeometry.h"
#include "emulator/video/zx/zxgeometry.h"

using namespace videomap;

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
} // namespace

VideoLayout AlcoVideoMapper::Layout(const VideoState& s) const
{
    const RasterDescriptor& t = s.timingDesc;
    const RasterDescriptor& l = s.layoutDesc;
    VideoLayout layout;
    layout.mapped = true;
    layout.family = Family();
    layout.mode = s.mode;
    layout.tstatesPerLine = static_cast<uint16_t>(t.pixelsPerLine / 2);
    layout.lines = static_cast<uint16_t>(t.vSyncLines + t.vBlankLines + t.fullFrameHeight);

    LayerDesc layer;
    layer.id = s.mode == M_P16 ? "p16" : "pmc";
    layer.surface = {ZxGeometry::kWidth, ZxGeometry::kHeight, static_cast<uint8_t>(s.mode == M_P16 ? 4 : 1)};
    layer.window.firstLine = static_cast<uint16_t>(t.vSyncLines + t.vBlankLines + t.screenOffsetTop);
    layer.window.lineCount = ZxGeometry::kHeight;
    layer.window.firstT = static_cast<uint16_t>(t.screenOffsetLeft / 2);
    layer.window.tCount = ZxGeometry::kWidth / 2;
    layer.window.dotsPerT = 2;
    layout.layers.push_back(layer);

    layout.fb = {l.fullFrameWidth, l.fullFrameHeight, l.screenOffsetLeft, l.screenOffsetTop};
    return layout;
}

bool AlcoVideoMapper::SourcesAt(const VideoState& s, const MemView& m, size_t layerIndex, uint32_t x, uint32_t y,
                                LayerContribution& out) const
{
    if (layerIndex != 0 || x >= ZxGeometry::kWidth || y >= ZxGeometry::kHeight)
        return false;

    const uint8_t videoPage = AtmGeometry::VideoPage(s.p7FFD);
    const uint8_t col = static_cast<uint8_t>(x / 8);
    const uint16_t line = ZxGeometry::PixelOffset(static_cast<uint8_t>(y), col);

    if (s.mode == M_P16)
    {
        const uint32_t q = (x / 2) & 3;  // plane q holds pixel pair q of the byte group
        const bool right = (x & 1) != 0;
        const SourceRef plane{Space::Ram, 0, static_cast<uint16_t>((q & 1) ? videoPage : (videoPage ^ 1)),
                              static_cast<uint32_t>(((q >> 1) << 13) + line), 1,
                              static_cast<uint16_t>(right ? 0xB8 : 0x47), SourceRole::Plane};
        out.layer = "p16";
        out.colourIndex = AtmGeometry::PairColourIndex(m.Read(plane), right);
        out.sources = {plane, PaletteCell(out.colourIndex)};
        out.rgb = PaletteRgb(s, out.colourIndex);
        return true;
    }

    // PMC: attribute at the pixel address + 0x2000; ink = bits 0-2 + bit 6, paper = bits 3-6, bit 7 blinks
    const uint16_t bit = static_cast<uint16_t>(0x80 >> (x % 8));
    const SourceRef pixel{Space::Ram, 0, videoPage, line, 1, bit, SourceRole::PixelBits};
    const uint8_t attr = m.Read({Space::Ram, 0, videoPage, static_cast<uint32_t>(line + 0x2000), 1, 0xFF,
                                 SourceRole::Attribute});
    bool set = (m.Read(pixel) & bit) != 0;
    if ((attr & 0x80) && s.flashPhase)
        set = !set;
    out.layer = "pmc";
    out.colourIndex = set ? static_cast<uint8_t>((attr & 0x07) | ((attr & 0x40) >> 3)) : static_cast<uint8_t>((attr & 0x78) >> 3);
    out.sources = {pixel,
                   {Space::Ram, 0, videoPage, static_cast<uint32_t>(line + 0x2000), 1,
                    static_cast<uint16_t>(set ? 0xC7 : 0xF8), SourceRole::Attribute},
                   PaletteCell(out.colourIndex)};
    out.rgb = PaletteRgb(s, out.colourIndex);
    return true;
}

void AlcoVideoMapper::BorderSources(const VideoState& s, LayerContribution& out) const
{
    out.layer = "border";
    out.colourIndex = static_cast<uint8_t>((s.borderAttr & 0x07) | (s.atmBorderBright ? 0x08 : 0x00));
    out.sources = {{Space::Register, 0, 0xFE, 0, 1, 0x07, SourceRole::Border}, PaletteCell(out.colourIndex)};
    out.rgb = PaletteRgb(s, out.colourIndex);
}

void AlcoVideoMapper::PixelsFor(const VideoState& s, const SourceRef& ref, std::vector<SurfaceArea>& out) const
{
    if (ref.space != Space::Ram)
        return;
    const uint8_t videoPage = AtmGeometry::VideoPage(s.p7FFD);
    const uint32_t base = ref.offset & 0x1FFF;
    if (base >= ZxGeometry::kPixelBytes || ref.offset >= 0x4000)
        return;
    uint8_t y = 0, col = 0;
    ZxGeometry::DecodePixelOffset(static_cast<uint16_t>(base), y, col);

    if (s.mode == M_P16)
    {
        if (ref.page != videoPage && ref.page != (videoPage ^ 1))
            return;
        const uint32_t q = (ref.page == videoPage ? 1u : 0u) | ((ref.offset >> 13) << 1);
        out.push_back({"p16", static_cast<uint16_t>(col * 8 + 2 * q), y, 2, 1});
        return;
    }
    if (ref.page == videoPage)
        out.push_back({"pmc", static_cast<uint16_t>(col * 8), y, 8, 1});
}
