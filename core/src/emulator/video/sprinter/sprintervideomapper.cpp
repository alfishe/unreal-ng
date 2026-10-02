#include "sprintervideomapper.h"

#include "emulator/video/sprinter/screensprinter.h"
#include "emulator/video/sprinter/sprintervideoram.h"
#include "emulator/video/sprinter/sprintervideorenderer.h"

using namespace videomap;

namespace
{
const SprinterVideoView* ViewOf(const VideoState& s)
{
    const auto* view = static_cast<const SprinterVideoView*>(s.familyView);
    return (view && view->vram && view->inputs.vram && view->inputs.palette) ? view : nullptr;
}

SourceRef Vram(uint32_t address, SourceRole role, uint16_t mask = 0xFF, uint8_t width = 1)
{
    return {Space::Vram, 0, 0, address & static_cast<uint32_t>(SprinterVideoRam::kSize - 1), width, mask, role};
}

/// The video RAM address a source names, or -1 (RAM pages #50-#5F are the CPU copy of video RAM)
int64_t VramAddressOf(const SourceRef& ref)
{
    if (ref.space == Space::Vram)
        return ref.offset & (SprinterVideoRam::kSize - 1);
    if (ref.space == Space::Ram && ref.page >= 0x50 && ref.page <= 0x5F && ref.offset < 0x4000)
        return static_cast<int64_t>(ref.page - 0x50) * 0x4000 + ref.offset;
    return -1;
}

bool Covers(const SourceRef& source, uint32_t address)
{
    return address >= source.offset && address < source.offset + source.width;
}
}  // namespace

uint32_t SprinterVideoMapper::Collect(const SprinterVideoInputs& in, uint32_t x, uint32_t y, std::vector<SourceRef>& sources)
{
    const uint32_t a16 = SprinterVideoRenderer::A16(in, x);
    const uint32_t b8 = SprinterVideoRenderer::B8(in, y);
    const uint32_t sub = a16 & 15;
    const uint32_t row = b8 & 7;
    const uint32_t line1 = SprinterVideoRam::ModeAddress(static_cast<uint8_t>(a16 >> 4), static_cast<uint8_t>(b8 >> 3), in.modePage);
    const uint8_t* mode = in.vram + line1;

    uint32_t pen;
    if (mode[0] & 0x10)
    {
        const uint8_t* used = SprinterVideoRenderer::SymbolMode(mode, sub);
        const uint32_t usedAddress = line1 + static_cast<uint32_t>(used - mode);
        for (uint32_t k = 0; k < 3; k++)
            sources.push_back(Vram(usedAddress + k, SourceRole::ModeDescriptor));
        const uint8_t m0 = used[0];
        if ((m0 & 0xFC) != 0xFC && (m0 >> 5) == 7)
        {
            sources.push_back({Space::Register, 0, 0xFE, 0, 1, 0x07, SourceRole::Border});
        }
        else if ((m0 & 0xFC) != 0xFC)
        {
            const uint32_t bit = 1u << (7 - ((sub >> ((m0 >> 5) & 1)) & 7));
            sources.push_back(Vram(SprinterVideoRenderer::FontAddress(in, used, row), SourceRole::FontRow, static_cast<uint16_t>(bit)));
            sources.push_back(Vram(SprinterVideoRenderer::AttrAddress(in, used), SourceRole::CharAttr));
        }
        pen = SprinterVideoRenderer::SymbolPen(in, mode, sub, row);
    }
    else
    {
        for (uint32_t k = 0; k < 3; k++)
            sources.push_back(Vram(line1 + k, SourceRole::ModeDescriptor));
        const uint16_t mask = (mode[0] & 0x20) ? 0xFF : ((sub & 1) ? 0x0F : 0xF0);
        sources.push_back(Vram(SprinterVideoRenderer::GraphicsAddress(mode, sub, row), SourceRole::PixelBits, mask));
        pen = SprinterVideoRenderer::GraphicsPen(in, mode, sub, row);
    }
    sources.push_back(Vram(SprinterVideoRam::PenAddress(pen), SourceRole::PaletteEntry, 0xFF, 3));
    return pen;
}

VideoLayout SprinterVideoMapper::Layout(const VideoState& s) const
{
    VideoLayout layout;
    layout.family = Family();
    layout.mode = s.mode;
    layout.tstatesPerLine = static_cast<uint16_t>(ScreenSprinter::kLineTStates);
    const SprinterVideoView* v = ViewOf(s);
    layout.lines = v ? v->inputs.lines : 320;
    if (!v)
        return layout;

    layout.mapped = true;
    LayerDesc layer;
    layer.id = kLayer;
    layer.surface = {static_cast<uint16_t>(SprinterVideoRenderer::kVisibleWidth),
                     static_cast<uint16_t>(SprinterVideoRenderer::kVisibleLines), 8};
    layer.window = {0, static_cast<uint16_t>(SprinterVideoRenderer::kVisibleLines), 0,
                    static_cast<uint16_t>(ScreenSprinter::kVisibleTStates), 4};
    layout.layers.push_back(layer);
    layout.fb = {static_cast<uint16_t>(SprinterVideoRenderer::kVisibleWidth),
                 static_cast<uint16_t>(SprinterVideoRenderer::kVisibleLines), 0, 0};
    return layout;
}

bool SprinterVideoMapper::SourcesAt(const VideoState& s, [[maybe_unused]] const MemView& m, size_t layerIndex, uint32_t x,
                                    uint32_t y, LayerContribution& out) const
{
    const SprinterVideoView* v = ViewOf(s);
    if (!v || layerIndex != 0 || x >= SprinterVideoRenderer::kVisibleWidth || y >= SprinterVideoRenderer::kVisibleLines)
        return false;
    out.layer = kLayer;
    const uint32_t pen = Collect(v->inputs, x, y, out.sources);
    out.colourIndex = static_cast<uint8_t>(pen & 0xFF);
    out.rgb = v->inputs.palette[pen & (SprinterVideoRam::kPens - 1)];
    return true;
}

void SprinterVideoMapper::BorderSources(const VideoState& s, LayerContribution& out) const
{
    const SprinterVideoView* v = ViewOf(s);
    if (!v)
        return;
    const uint32_t pen = SprinterVideoRenderer::kPenText | (static_cast<uint32_t>(v->inputs.border & 7) * 9u);
    out.layer = "border";
    out.sources.push_back({Space::Register, 0, 0xFE, 0, 1, 0x07, SourceRole::Border});
    out.sources.push_back(Vram(SprinterVideoRam::PenAddress(pen), SourceRole::PaletteEntry, 0xFF, 3));
    out.colourIndex = static_cast<uint8_t>(pen & 0xFF);
    out.rgb = v->inputs.palette[pen];
}

void SprinterVideoMapper::PixelsFor(const VideoState& s, const SourceRef& ref, std::vector<SurfaceArea>& out) const
{
    const SprinterVideoView* v = ViewOf(s);
    const int64_t address = VramAddressOf(ref);
    if (!v || address < 0)
        return;

    // Debug path, brute force: every visible pixel's sources, runs merged per line
    std::vector<SourceRef> sources;
    for (uint32_t y = 0; y < SprinterVideoRenderer::kVisibleLines; y++)
    {
        int64_t runStart = -1;
        for (uint32_t x = 0; x <= SprinterVideoRenderer::kVisibleWidth; x++)
        {
            bool hit = false;
            if (x < SprinterVideoRenderer::kVisibleWidth)
            {
                sources.clear();
                Collect(v->inputs, x, y, sources);
                for (const SourceRef& source : sources)
                {
                    if (source.space == Space::Vram && Covers(source, static_cast<uint32_t>(address)))
                    {
                        hit = true;
                        break;
                    }
                }
            }
            if (hit && runStart < 0)
                runStart = x;
            if (!hit && runStart >= 0)
            {
                out.push_back({kLayer, static_cast<uint16_t>(runStart), static_cast<uint16_t>(y),
                               static_cast<uint16_t>(x - runStart), 1});
                runStart = -1;
            }
        }
    }
}
