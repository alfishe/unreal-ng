#include "stdafx.h"

/// @file devicestatevideo.cpp
/// @brief Video debug translation reports (PLAN #42 phase 2): the beam, the
/// layout, pixel sources, byte -> pixels and text grids, built once from
/// VideoMapService and rendered by every automation interface (WebAPI, MCP,
/// CLI, Lua, Python) through its StateNode converter - so field names and
/// values cannot drift between them (design G6).

#include <cstdio>

#include "emulator/config.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/state/devicestate.h"
#include "emulator/video/map/videomapservice.h"
#include "emulator/video/screen.h"

using namespace videomap;

namespace
{
StateNode Unavailable(const char* description)
{
    StateNode n = StateNode::Object();
    n["available"] = false;
    n["description"] = description;
    return n;
}

const char* SpaceName(Space space)
{
    switch (space)
    {
        case Space::Ram:           return "ram";
        case Space::Rom:           return "rom";
        case Space::Vram:          return "vram";
        case Space::Palette:       return "palette";
        case Space::SpriteRam:     return "sprite_ram";
        case Space::TileRam:       return "tile_ram";
        case Space::CopperRam:     return "copper_ram";
        case Space::Register:      return "register";
        case Space::InternalTable: return "internal_table";
    }
    return "unknown";
}

const char* RoleName(SourceRole role)
{
    switch (role)
    {
        case SourceRole::PixelBits:        return "pixel_bits";
        case SourceRole::Attribute:        return "attribute";
        case SourceRole::Plane:            return "plane";
        case SourceRole::CharCode:         return "char_code";
        case SourceRole::CharAttr:         return "char_attr";
        case SourceRole::FontRow:          return "font_row";
        case SourceRole::TileDescriptor:   return "tile_descriptor";
        case SourceRole::TileGraphic:      return "tile_graphic";
        case SourceRole::SpriteDescriptor: return "sprite_descriptor";
        case SourceRole::SpriteGraphic:    return "sprite_graphic";
        case SourceRole::PaletteEntry:     return "palette_entry";
        case SourceRole::Border:           return "border";
        case SourceRole::ModeDescriptor:   return "mode_descriptor";
        case SourceRole::Selector:         return "selector";
    }
    return "unknown";
}

/// ABGR (framebuffer order) -> "#RRGGBB"
std::string RgbText(uint32_t abgr)
{
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%02X%02X%02X", abgr & 0xFF, (abgr >> 8) & 0xFF, (abgr >> 16) & 0xFF);
    return buf;
}

std::string Hex(uint32_t value, int digits)
{
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%0*X", digits, value);
    return buf;
}

StateNode SourceNode(const VideoMapService& service, const SourceRef& ref)
{
    StateNode n = StateNode::Object();
    n["space"] = SpaceName(ref.space);
    n["page"] = int(ref.page);
    n["offset"] = Hex(ref.offset, 4);
    n["width"] = int(ref.width);
    n["bit_mask"] = Hex(ref.bitMask, 2);
    n["role"] = RoleName(ref.role);
    if (ref.space == Space::Ram)
    {
        StateNode z80 = StateNode::Array();
        for (uint16_t address : service.Z80Aliases(ref))
            z80.items.push_back(StateNode(Hex(address, 4)));
        n["z80"] = z80;
    }
    return n;
}

StateNode PixelNode(const VideoMapService& service, const PixelSources& p, uint32_t x, uint32_t y)
{
    if (!p.valid)
        return Unavailable("No layer pixel or border at this point in the current video mode");
    StateNode n = StateNode::Object();
    n["available"] = true;
    n["border"] = p.border;
    n["layer"] = p.contribution.layer;
    if (!p.border)
    {
        n["x"] = x;
        n["y"] = y;
    }
    StateNode sources = StateNode::Array();
    for (const SourceRef& ref : p.contribution.sources)
        sources.items.push_back(SourceNode(service, ref));
    n["sources"] = sources;
    n["colour_index"] = int(p.contribution.colourIndex);
    n["rgb"] = RgbText(p.finalRgb);
    if (p.renderedKnown)
        n["rendered_rgb"] = RgbText(p.renderedRgb);
    // Design §4.6: addresses follow the latches at state_at (a frame T from the
    // video write log, or "current"); colours come from memory and palettes now
    if (p.stateAtT >= 0)
    {
        n["state_at"] = static_cast<int64_t>(p.stateAtT);
        n["state_frame"] = static_cast<uint64_t>(p.stateFrame);
        n["state_partial"] = p.statePartial;
    }
    else
    {
        n["state_at"] = "current";
    }
    n["values_at"] = "current";
    n["snapshot"] = p.fromSnapshot;
    return n;
}
} // namespace

namespace DeviceState
{
StateNode VideoBeam(EmulatorContext* context)
{
    if (!context || !context->pScreen)
        return Unavailable("Screen not available");
    const CONFIG& config = context->config;
    if (config.t_line == 0 || config.frame == 0)
        return Unavailable("Machine model timing is not initialized yet");

    ::Screen* screen = context->pScreen;
    Z80* cpu = context->pCore ? context->pCore->GetZ80() : nullptr;
    const uint32_t tstate = cpu ? static_cast<uint32_t>(cpu->t) : screen->GetCurrentTstate();
    const uint32_t tInFrame = tstate % config.frame;

    const VideoModeEnum mode = screen->GetVideoMode();
    const RasterDescriptor& rd = screen->rasterDescriptors[mode];
    const RasterDescriptor& timingRd = screen->GetTimingDescriptor(mode);
    const RasterState& rs = screen->GetRasterState();
    const bool rasterValid = rs.tstatesPerLine != 0;
    const uint32_t tstatesPerLine = rasterValid ? rs.tstatesPerLine : config.t_line;
    const uint32_t totalLines = timingRd.vSyncLines + timingRd.vBlankLines + timingRd.fullFrameHeight;

    const VideoMapService service(context);
    const BeamInfo info = service.BeamAt(tInFrame);
    const BeamPosition& beam = info.beam;

    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["model"] = Config::GetModelFullName(config.mem_model);
    ret["video_mode"] = ::Screen::GetVideoModeName(mode);
    ret["tstate"] = tstate;
    ret["tstate_in_frame"] = tInFrame;
    ret["frame"] = static_cast<uint64_t>(context->emulatorState.frame_counter);
    ret["line"] = tInFrame / tstatesPerLine;
    ret["dot_in_line"] = tInFrame % tstatesPerLine;
    ret["beam_x"] = beam.beamX;
    ret["beam_y"] = tInFrame / tstatesPerLine;
    ret["zone"] = beam.zone;
    ret["vertical_zone"] = beam.verticalZone;
    ret["horizontal_zone"] = beam.horizontalZone;
    ret["in_visible_area"] = beam.inVisibleArea;
    ret["in_paper"] = beam.inPaper;
    if (beam.inPaper)
    {
        // Mode pixels under the beam; one T covers x..x_end
        StateNode paper = StateNode::Object();
        paper["x"] = beam.paperX;
        paper["x_end"] = beam.paperXEnd;
        paper["y"] = beam.paperY;
        ret["paper"] = paper;
    }

    // The layer points under the beam (design §6: layers[])
    StateNode layers = StateNode::Array();
    if (info.inLayer)
    {
        StateNode layer = StateNode::Object();
        layer["id"] = info.layer;
        layer["x"] = info.x;
        layer["x_end"] = info.xEnd;
        layer["y"] = info.y;
        layers.items.push_back(layer);
    }
    ret["layers"] = layers;

    StateNode timing = StateNode::Object();
    timing["tstates_per_line"] = unsigned(config.t_line);
    timing["lines_per_frame"] = totalLines;
    timing["frame_tstates"] = unsigned(config.frame);
    timing["raster_frame_tstates"] = tstatesPerLine * totalLines;  // raster-defined duration; config.frame may pad it
    timing["frame_duration_us"] = unsigned(config.frame_duration_us);
    timing["frames_per_second"] = int(config.intfq);
    timing["cpu_hz"] = static_cast<double>(config.frame) * config.intfq;
    timing["frequency_multiplier"] = int(context->emulatorState.current_z80_frequency_multiplier);
    ret["frame_timing"] = timing;

    StateNode raster = StateNode::Object();
    raster["full_frame_width"] = int(rd.fullFrameWidth);
    raster["full_frame_height"] = int(rd.fullFrameHeight);
    raster["screen_width"] = int(rd.screenWidth);
    raster["screen_height"] = int(rd.screenHeight);
    raster["screen_offset_left"] = int(rd.screenOffsetLeft);
    raster["screen_offset_top"] = int(rd.screenOffsetTop);
    raster["pixels_per_line"] = int(timingRd.pixelsPerLine);
    raster["h_sync_pixels"] = int(timingRd.hSyncPixels);
    raster["h_blank_pixels"] = int(timingRd.hBlankPixels);
    raster["v_sync_lines"] = int(timingRd.vSyncLines);
    raster["v_blank_lines"] = int(timingRd.vBlankLines);
    raster["paper_start_t"] = rs.screenLineAreaStart;
    raster["paper_end_t"] = rs.screenLineAreaEnd;
    raster["paper_dots_per_t"] = int(rs.paperDotsPerT);
    raster["total_lines"] = totalLines;
    ret["raster"] = raster;
    return ret;
}

StateNode VideoLayout(EmulatorContext* context)
{
    if (!context || !context->pScreen)
        return Unavailable("Screen not available");
    const videomap::VideoLayout layout = VideoMapService(context).Layout();

    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["mapped"] = layout.mapped;
    ret["family"] = layout.family;
    ret["video_mode"] = ::Screen::GetVideoModeName(layout.mode);
    ret["tstates_per_line"] = int(layout.tstatesPerLine);
    ret["lines"] = int(layout.lines);
    StateNode layers = StateNode::Array();
    for (const LayerDesc& l : layout.layers)
    {
        StateNode layer = StateNode::Object();
        layer["id"] = l.id;
        StateNode surface = StateNode::Object();
        surface["width"] = int(l.surface.width);
        surface["height"] = int(l.surface.height);
        surface["bits_per_pixel"] = int(l.surface.bitsPerPixel);
        if (l.surface.textColumns)
        {
            surface["text_columns"] = int(l.surface.textColumns);
            surface["text_rows"] = int(l.surface.textRows);
            surface["cell_width"] = int(l.surface.cellWidth);
            surface["cell_height"] = int(l.surface.cellHeight);
        }
        layer["surface"] = surface;
        StateNode window = StateNode::Object();
        window["first_line"] = int(l.window.firstLine);
        window["line_count"] = int(l.window.lineCount);
        window["first_t"] = int(l.window.firstT);
        window["t_count"] = int(l.window.tCount);
        window["dots_per_t"] = int(l.window.dotsPerT);
        layer["window"] = window;
        layers.items.push_back(layer);
    }
    ret["layers"] = layers;
    StateNode fb = StateNode::Object();
    fb["width"] = int(layout.fb.width);
    fb["height"] = int(layout.fb.height);
    fb["surface_left"] = int(layout.fb.surfaceLeft);
    fb["surface_top"] = int(layout.fb.surfaceTop);
    ret["framebuffer"] = fb;
    return ret;
}

StateNode VideoPixel(EmulatorContext* context, unsigned layer, unsigned x, unsigned y)
{
    if (!context || !context->pScreen)
        return Unavailable("Screen not available");
    const VideoMapService service(context);
    return PixelNode(service, service.SourcesAt(layer, x, y), x, y);
}

StateNode VideoPixelAtBeam(EmulatorContext* context, unsigned tInFrame)
{
    if (!context || !context->pScreen)
        return Unavailable("Screen not available");
    const VideoMapService service(context);
    const BeamInfo beam = service.BeamAt(tInFrame);
    StateNode n = PixelNode(service, service.SourcesAtBeam(tInFrame), beam.x, beam.y);
    n["tstate_in_frame"] = tInFrame;
    return n;
}

namespace
{
StateNode AreasNode(const std::vector<SurfaceArea>& areas)
{
    StateNode list = StateNode::Array();
    for (const SurfaceArea& a : areas)
    {
        StateNode n = StateNode::Object();
        n["layer"] = a.layer;
        n["x"] = int(a.x);
        n["y"] = int(a.y);
        n["width"] = int(a.width);
        n["height"] = int(a.height);
        list.items.push_back(n);
    }
    return list;
}
} // namespace

StateNode VideoAddress(EmulatorContext* context, unsigned page, unsigned offset)
{
    if (!context || !context->pScreen)
        return Unavailable("Screen not available");
    if (offset >= 0x4000)
        return Unavailable("offset must be below 0x4000 (one 16 KB page)");
    const VideoMapService service(context);
    const std::vector<SurfaceArea> areas =
        service.PixelsFor({Space::Ram, 0, static_cast<uint16_t>(page), offset, 1, 0xFF, SourceRole::PixelBits});
    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["space"] = "ram";
    ret["page"] = int(page);
    ret["offset"] = Hex(offset, 4);
    ret["feeds_picture"] = !areas.empty();
    ret["areas"] = AreasNode(areas);
    return ret;
}

StateNode VideoAddressIn(EmulatorContext* context, const std::string& space, unsigned page, unsigned offset)
{
    if (space.empty() || space == "ram")
        return VideoAddress(context, page, offset);
    if (!context || !context->pScreen)
        return Unavailable("Screen not available");
    Space where;
    if (space == "sprite_ram")
        where = Space::SpriteRam;
    else if (space == "palette")
        where = Space::Palette;
    else
    {
        const std::string reason = "space '" + space + "': expected ram, sprite_ram or palette";
        return Unavailable(reason.c_str());
    }
    const VideoMapService service(context);
    const std::vector<SurfaceArea> areas = service.PixelsFor({where, 0, 0, offset, 2, 0xFFFF, SourceRole::PaletteEntry});
    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["space"] = space;
    ret["offset"] = Hex(offset, 4);
    ret["feeds_picture"] = !areas.empty();
    ret["areas"] = AreasNode(areas);
    return ret;
}

StateNode VideoAddressZ80(EmulatorContext* context, unsigned address)
{
    if (!context || !context->pScreen)
        return Unavailable("Screen not available");
    const VideoMapService service(context);
    const std::vector<SurfaceArea> areas = service.PixelsForZ80(static_cast<uint16_t>(address & 0xFFFF));
    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["z80"] = Hex(address & 0xFFFF, 4);
    ret["feeds_picture"] = !areas.empty();
    ret["areas"] = AreasNode(areas);
    return ret;
}

StateNode VideoText(EmulatorContext* context, unsigned layer)
{
    if (!context || !context->pScreen)
        return Unavailable("Screen not available");
    const VideoMapService service(context);
    uint16_t columns = 0, rows = 0;
    std::vector<TextCell> cells;
    if (!service.Text(layer, columns, rows, cells))
        return Unavailable("The current video mode has no text layer (bitmap modes: use screen_ocr)");

    const videomap::VideoLayout layout = service.Layout();
    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["layer"] = layout.layers[layer].id;
    ret["columns"] = int(columns);
    ret["rows"] = int(rows);
    // One entry per row: printable text ('.' for other codes) and the raw codes / attributes as hex
    StateNode lines = StateNode::Array();
    for (uint16_t r = 0; r < rows; ++r)
    {
        std::string text, codes, attrs;
        for (uint16_t c = 0; c < columns; ++c)
        {
            const TextCell& cell = cells[static_cast<size_t>(r) * columns + c];
            text += (cell.code >= 0x20 && cell.code < 0x7F) ? static_cast<char>(cell.code) : '.';
            char buf[3];
            std::snprintf(buf, sizeof buf, "%02X", cell.code);
            codes += buf;
            std::snprintf(buf, sizeof buf, "%02X", cell.attr);
            attrs += buf;
        }
        StateNode line = StateNode::Object();
        line["text"] = text;
        line["codes"] = codes;
        line["attrs"] = attrs;
        lines.items.push_back(line);
    }
    ret["lines"] = lines;
    return ret;
}
} // namespace DeviceState
