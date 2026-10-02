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
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/devicememory.h"
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
    if (space == "vram")
    {
        if (!DeviceMemory::Find(context, "vram"))
            return Unavailable("space 'vram': this machine has no video RAM of its own (its screen is in RAM pages)");
        // A device's own video RAM (the Sprinter's 256 KB): the address is page x 16 KB + offset
        const uint32_t address = page * 0x4000u + offset;
        const VideoMapService service(context);
        const std::vector<SurfaceArea> areas = service.PixelsFor({Space::Vram, 0, 0, address, 1, 0xFF, SourceRole::PixelBits});
        StateNode ret = StateNode::Object();
        ret["available"] = true;
        ret["space"] = space;
        ret["address"] = Hex(address, 5);
        ret["feeds_picture"] = !areas.empty();
        ret["areas"] = AreasNode(areas);
        return ret;
    }
    Space where;
    if (space == "sprite_ram")
        where = Space::SpriteRam;
    else if (space == "palette")
        where = Space::Palette;
    else
    {
        const std::string reason = "space '" + space + "': expected ram, vram, sprite_ram or palette";
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
    {
        // The Sprinter: its text squares as an 80 x 32 grid (SprinterText, the renderer's classifier),
        // while the picture has any - an agent that does not know the machine still reads BIOS / DSS screens
        const StateNode sprinter = SprinterText(context);
        const StateNode* squares = sprinter.find("text_squares");
        const StateNode* spectrum = sprinter.find("spectrum_screen");
        if (spectrum && spectrum->b)
            return Unavailable("Sprinter in Spectrum mode: the picture is a ZX screen (use screen_ocr)");
        if (layer == 0 && squares && squares->i > 0)
        {
            StateNode ret = StateNode::Object();
            ret["available"] = true;
            ret["layer"] = "sprinter_text";
            ret["source"] = "the Sprinter mode table's text squares (GET /state/sprinter/text)";
            ret["columns"] = 80;
            ret["rows"] = 32;
            StateNode lines = StateNode::Array();
            if (const StateNode* from = sprinter.find("lines"))
            {
                for (const StateNode& line : from->items)
                {
                    StateNode n = StateNode::Object();
                    const StateNode* text = line.find("text");
                    const StateNode* codes = line.find("codes");
                    n["text"] = text ? text->s : std::string();
                    n["codes"] = codes ? codes->s : std::string();
                    lines.items.push_back(n);
                }
            }
            ret["lines"] = lines;
            return ret;
        }
        return Unavailable("The current video mode has no text layer (bitmap modes: use screen_ocr)");
    }

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

namespace
{
/// One latch of VideoLatches by name: how the change log shows it
struct LatchField
{
    const char* name;
    unsigned (*get)(const VideoLatches&);
    bool sprinter;  ///< the Sprinter family block: shown only where a Sprinter screen fills it
};

const LatchField kLatchFields[] = {
    {"mode", [](const VideoLatches& l) { return unsigned(l.mode); }, false},
    {"port_7ffd", [](const VideoLatches& l) { return unsigned(l.p7FFD); }, false},
    {"port_eff7", [](const VideoLatches& l) { return unsigned(l.pEFF7); }, false},
    {"port_ff77", [](const VideoLatches& l) { return unsigned(l.pFF77); }, false},
    {"port_dffd", [](const VideoLatches& l) { return unsigned(l.pDFFD); }, false},
    {"atm_fe_address", [](const VideoLatches& l) { return unsigned(l.aFE); }, false},
    {"port_fe", [](const VideoLatches& l) { return unsigned(l.pFE); }, false},
    {"border_attr", [](const VideoLatches& l) { return unsigned(l.borderAttr); }, false},
    {"border", [](const VideoLatches& l) { return unsigned(l.borderIndex); }, false},
    {"atm_border_bright", [](const VideoLatches& l) { return unsigned(l.atmBorderBright); }, false},
    {"active_screen", [](const VideoLatches& l) { return unsigned(l.activeScreen); }, false},
    {"rgmod", [](const VideoLatches& l) { return unsigned(l.rgMod); }, true},
    {"hold", [](const VideoLatches& l) { return unsigned(l.hold); }, true},
    {"port_y", [](const VideoLatches& l) { return unsigned(l.portY); }, true},
    {"all_mode", [](const VideoLatches& l) { return unsigned(l.allMode); }, true},
    {"frame_lines", [](const VideoLatches& l) { return unsigned(l.frameLines); }, true},
};

std::string LatchValue(const LatchField& field, unsigned value)
{
    if (std::string(field.name) == "mode")
        return ::Screen::GetVideoModeName(static_cast<VideoModeEnum>(value));
    if (std::string(field.name) == "frame_lines" || std::string(field.name) == "border" ||
        std::string(field.name) == "active_screen")
        return std::to_string(value);
    return Hex(value, 2);
}

StateNode LatchesNode(const VideoLatches& l, bool sprinter)
{
    StateNode n = StateNode::Object();
    for (const LatchField& field : kLatchFields)
        if (!field.sprinter || sprinter)
            n[field.name] = LatchValue(field, field.get(l));
    return n;
}

StateNode TableNode(const VideoTableWrites& w)
{
    StateNode n = StateNode::Object();
    n["count"] = static_cast<uint64_t>(w.count);
    if (w.count)
    {
        n["first_t"] = static_cast<uint64_t>(w.firstT);
        n["last_t"] = static_cast<uint64_t>(w.lastT);
        n["first_address"] = Hex(w.firstAddress, 5);
        n["last_address"] = Hex(w.lastAddress, 5);
        n["first_pc"] = Hex(w.firstPc, 4);
        n["last_pc"] = Hex(w.lastPc, 4);
    }
    return n;
}

StateNode FrameNode(::Screen& screen, const VideoFrameLog& log, bool current, bool sprinter)
{
    StateNode f = StateNode::Object();
    f["frame"] = static_cast<uint64_t>(log.frame);
    f["current"] = current;
    f["partial"] = log.partial;
    f["start"] = LatchesNode(log.start, sprinter);
    StateNode writes = StateNode::Array();
    VideoLatches before = log.start;
    for (const VideoWrite& w : log.writes)
    {
        StateNode e = StateNode::Object();
        const BeamPosition beam = screen.DescribeBeam(w.t);
        e["t"] = static_cast<uint64_t>(w.t);
        e["line"] = static_cast<uint64_t>(beam.line);
        e["t_in_line"] = static_cast<uint64_t>(beam.tInLine);
        e["pc"] = Hex(w.pc, 4);
        StateNode changes = StateNode::Object();
        for (const LatchField& field : kLatchFields)
        {
            const unsigned from = field.get(before);
            const unsigned to = field.get(w.latches);
            if (from != to)
                changes[field.name] = LatchValue(field, from) + " -> " + LatchValue(field, to);
        }
        e["changes"] = changes;
        writes.push(e);
        before = w.latches;
    }
    f["writes"] = writes;
    StateNode tables = StateNode::Object();
    tables["mode_table"] = TableNode(log.tables[static_cast<size_t>(VideoTable::ModeTable)]);
    tables["palette"] = TableNode(log.tables[static_cast<size_t>(VideoTable::Palette)]);
    f["tables"] = tables;
    return f;
}
}  // namespace

namespace DeviceState
{
StateNode VideoChanges(EmulatorContext* context, unsigned frames)
{
    if (!context || !context->pScreen)
        return Unavailable("Screen not available");
    ::Screen& screen = *context->pScreen;
    const VideoWriteLog& log = screen.GetVideoWriteLog();
    // The family block of the latches (RGMOD ...) is shown where the machine's screen fills it
    const bool sprinter = screen.GetVideoMode() == M_SPRINTER;
    Emulator* emulator = context->pEmulator;
    const bool running = emulator && emulator->IsRunning() && !emulator->IsPaused();

    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["running"] = running;
    ret["about"] = "video latch changes at their port write (frame T, beam line / T in line, PC, the latches that "
                   "changed) and per-frame counts of palette / mode table writes; frames=1 the last completed frame, "
                   "frames=2 (default) the current one too while the machine is paused";
    StateNode list = StateNode::Array();
    if (running)
    {
        const VideoFrameLog published = log.Published();
        if (published.valid)
            list.push(FrameNode(screen, published, false, sprinter));
    }
    else
    {
        if (log.Previous().valid)
            list.push(FrameNode(screen, log.Previous(), false, sprinter));
        if (frames > 1 && log.Current().valid)
            list.push(FrameNode(screen, log.Current(), true, sprinter));
    }
    ret["frames"] = list;
    ret["see"] = "port writes themselves: the port trace (Sprinter codes RgMod, Hold, PortY, AllMode, Frame320, Frame312, "
                 "Border); the state at a T: /video/pixel?t=";
    return ret;
}
} // namespace DeviceState

