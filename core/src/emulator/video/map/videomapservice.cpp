#include "videomapservice.h"

#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/video/alco/alcovideomapper.h"
#include "emulator/video/atm/atmfont.h"
#include "emulator/video/atm/atmvideomapper.h"
#include "emulator/video/profi/profivideomapper.h"
#include "emulator/video/screen.h"
#include "emulator/video/zx/zxvideomapper.h"

namespace videomap
{
uint8_t MemView::Read(const SourceRef& ref) const
{
    switch (ref.space)
    {
        case Space::Ram:
        {
            if (!_memory || ref.offset >= 0x4000)
                return 0;
            const uint8_t* page = _memory->RAMPageAddress(ref.page);
            return page ? page[ref.offset] : 0;
        }
        case Space::InternalTable:
            return ref.offset < sizeof(ATM_FONT) ? ATM_FONT[ref.offset] : 0;
        default:
            return 0;
    }
}

VideoLayout NullVideoMapper::Layout(const VideoState& s) const
{
    VideoLayout layout;
    layout.family = Family();
    layout.mode = s.mode;
    layout.tstatesPerLine = static_cast<uint16_t>(s.timingDesc.pixelsPerLine / 2);
    layout.lines = static_cast<uint16_t>(s.timingDesc.vSyncLines + s.timingDesc.vBlankLines + s.timingDesc.fullFrameHeight);
    return layout;
}

VideoMapService::VideoMapService(EmulatorContext* context) : _context(context)
{
}

const IVideoMapper& VideoMapService::MapperFor(VideoFamily family)
{
    static const ZxVideoMapper zx;
    static const AlcoVideoMapper alco;
    static const AtmVideoMapper atm;
    static const ProfiVideoMapper profi;
    static const NullVideoMapper none;
    switch (family)
    {
        case VideoFamily::Zx:    return zx;
        case VideoFamily::Alco:  return alco;
        case VideoFamily::Atm:   return atm;
        case VideoFamily::Profi: return profi;
        default:                 return none;
    }
}

VideoState VideoMapService::State() const
{
    VideoState s;
    if (!_context)
        return s;
    const EmulatorState& state = _context->emulatorState;
    const CONFIG& config = _context->config;
    Screen* screen = _context->pScreen;

    s.model = config.mem_model;
    s.p7FFD = state.p7FFD;
    s.pFE = state.pFE;
    s.borderAttr = state.border_attr;
    s.atmBorderBright = state.atmBorderBright != 0;
    s.profiMonochrome = config.profi_monochrome != 0;
    s.atmPalette = state.atmPalette;
    s.profiPalette = state.profiPalette;
    s.zxScreenPage = Screen::GetVideoRAMPage(s.model, s.p7FFD);
    if (_context->pMemory)
        s.ramMask = _context->pMemory->GetRamMask();
    if (screen)
    {
        s.mode = screen->GetVideoMode();
        s.borderIndex = screen->GetBorderColor();
        s.flashPhase = screen->_vid.flash != 0;
        if (s.mode < M_MAX)
        {
            s.layoutDesc = screen->rasterDescriptors[s.mode];
            s.timingDesc = screen->GetTimingDescriptor(s.mode);
        }
    }
    return s;
}

VideoLayout VideoMapService::Layout() const
{
    const VideoState s = State();
    return MapperFor(FamilyOf(s.mode)).Layout(s);
}

BeamInfo VideoMapService::BeamAt(uint32_t tInFrame) const
{
    BeamInfo info;
    if (!_context || !_context->pScreen)
        return info;
    info.beam = _context->pScreen->DescribeBeam(tInFrame);
    if (!info.beam.valid)
        return info;

    const VideoLayout layout = Layout();
    for (const LayerDesc& layer : layout.layers)
    {
        const LayerWindow& w = layer.window;
        if (info.beam.line < w.firstLine || info.beam.line >= static_cast<uint32_t>(w.firstLine + w.lineCount))
            continue;
        if (info.beam.tInLine < w.firstT || info.beam.tInLine >= static_cast<uint32_t>(w.firstT + w.tCount))
            continue;
        info.inLayer = true;
        info.layer = layer.id;
        info.x = (info.beam.tInLine - w.firstT) * w.dotsPerT;
        info.xEnd = info.x + w.dotsPerT - 1;
        info.y = info.beam.line - w.firstLine;
        break;
    }
    return info;
}

std::vector<uint16_t> VideoMapService::Z80Aliases(const SourceRef& ref) const
{
    std::vector<uint16_t> result;
    if (ref.space != Space::Ram || ref.offset >= 0x4000 || !_context || !_context->pMemory)
        return result;
    Memory* memory = _context->pMemory;
    for (uint8_t bank = 0; bank < 4; ++bank)
    {
        if (memory->GetRAMPageForBank(bank) == ref.page)
            result.push_back(static_cast<uint16_t>(bank * 0x4000 + ref.offset));
    }
    return result;
}

PixelSources VideoMapService::SourcesAt(size_t layerIndex, uint32_t x, uint32_t y) const
{
    PixelSources result;
    const VideoState s = State();
    const IVideoMapper& mapper = MapperFor(FamilyOf(s.mode));
    const MemView memory(_context ? _context->pMemory : nullptr);
    if (!mapper.SourcesAt(s, memory, layerIndex, x, y, result.contribution))
        return result;

    result.valid = true;
    result.finalRgb = result.contribution.rgb;
    if (!result.contribution.sources.empty())
        result.z80 = Z80Aliases(result.contribution.sources.front());

    // What the framebuffer holds: its geometry is the current mode's
    Screen* screen = _context->pScreen;
    const VideoLayout layout = mapper.Layout(s);
    if (screen && screen->GetVideoMode() == s.mode)
    {
        FramebufferDescriptor& fb = screen->GetFramebufferDescriptor();
        const uint32_t fx = layout.fb.surfaceLeft + x;
        const uint32_t fy = layout.fb.surfaceTop + y;
        if (fb.memoryBuffer && fx < fb.width && fy < fb.height)
        {
            result.renderedRgb = reinterpret_cast<const uint32_t*>(fb.memoryBuffer)[fy * fb.width + fx];
            result.renderedKnown = true;
        }
    }
    return result;
}

PixelSources VideoMapService::SourcesAtBeam(uint32_t tInFrame) const
{
    const BeamInfo beam = BeamAt(tInFrame);
    if (beam.inLayer)
        return SourcesAt(0, beam.x, beam.y);

    PixelSources result;
    if (!beam.beam.valid)
        return result;
    // Border: the border rows and the sides of the paper rows, up to the end of the right border
    // (BeamPosition::inVisibleArea covers the paper rows only)
    const std::string vertical = beam.beam.verticalZone;
    const bool borderRows = vertical == "top_border" || vertical == "bottom_border";
    const bool paperRowSides = vertical == "screen" && beam.beam.inVisibleArea;
    const bool withinLine = beam.beam.tInLine <= _context->pScreen->GetRasterState().rightBorderAreaEnd;
    if (!(paperRowSides || (borderRows && withinLine)))
        return result;
    const VideoState s = State();
    MapperFor(FamilyOf(s.mode)).BorderSources(s, result.contribution);
    result.valid = !result.contribution.layer.empty();
    result.border = true;
    result.finalRgb = result.contribution.rgb;
    return result;
}

std::vector<SurfaceArea> VideoMapService::PixelsFor(const SourceRef& ref) const
{
    std::vector<SurfaceArea> out;
    const VideoState s = State();
    MapperFor(FamilyOf(s.mode)).PixelsFor(s, ref, out);
    return out;
}

std::vector<SurfaceArea> VideoMapService::PixelsForZ80(uint16_t address) const
{
    if (!_context || !_context->pMemory)
        return {};
    const uint16_t page = _context->pMemory->GetRAMPageForBank(static_cast<uint8_t>(address >> 14));
    if (page == MEMORY_UNMAPPABLE)
        return {};
    return PixelsFor({Space::Ram, 0, page, static_cast<uint32_t>(address & 0x3FFF), 1, 0xFF, SourceRole::PixelBits});
}

bool VideoMapService::Text(size_t layerIndex, uint16_t& columns, uint16_t& rows, std::vector<TextCell>& cells) const
{
    const VideoState s = State();
    const IVideoMapper& mapper = MapperFor(FamilyOf(s.mode));
    const VideoLayout layout = mapper.Layout(s);
    if (layerIndex >= layout.layers.size() || layout.layers[layerIndex].surface.textColumns == 0)
        return false;
    const MemView memory(_context->pMemory);
    columns = layout.layers[layerIndex].surface.textColumns;
    rows = layout.layers[layerIndex].surface.textRows;
    cells.assign(static_cast<size_t>(columns) * rows, TextCell{});
    for (uint16_t r = 0; r < rows; ++r)
        for (uint16_t c = 0; c < columns; ++c)
            mapper.TextAt(s, memory, layerIndex, c, r, cells[static_cast<size_t>(r) * columns + c]);
    return true;
}
} // namespace videomap
