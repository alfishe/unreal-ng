#include "tsconfvideomapper.h"


#include "emulator/emulatorcontext.h"
#include "emulator/platforms/tsconf/tsconfengine.h"
#include "emulator/platforms/tsconf/tsconfgeometry.h"
#include "emulator/platforms/tsconf/tsconfstate.h"
#include "emulator/platforms/tsconf/tsconftsu.h"
#include "emulator/video/tsconf/screentsconf.h"

using namespace videomap;

namespace
{
constexpr uint32_t kFirstVisibleDot = TsConfGeometry::kFirstVisibleDot;
constexpr uint32_t kFirstVisibleLine = TsConfGeometry::kFirstVisibleLine;

const TsConfVideoView* ViewOf(const VideoState& s)
{
    const auto* view = static_cast<const TsConfVideoView*>(s.familyView);
    return (view && view->ts && view->engine) ? view : nullptr;
}

/// The V_CONFIG the frame is shown with: the current register (the window and
/// the layer follow it; each line's own set decides its bytes)
uint8_t CurrentVConfig(const TsConfVideoView& v)
{
    return v.ts->regs[TsConfReg::VConfig];
}

bool IsText(uint8_t vConfig)
{
    return (vConfig & 0x03) == 3;
}

SourceRef CramCell(uint8_t index)
{
    return {Space::Palette, 0, 0, static_cast<uint32_t>(index) * 2u, 2, 0xFFFF, SourceRole::PaletteEntry};
}

SourceRef RamByte(uint32_t physical, uint16_t mask, SourceRole role)
{
    return {Space::Ram, 0, static_cast<uint16_t>(physical >> 14), physical & 0x3FFFu, 1, mask, role};
}

/// Physical address of a ZX-layout pixel / attribute byte at V_PAGE for graphics (gx, gy)
uint32_t ZxPixelAddress(uint8_t vPage, uint32_t gx, uint32_t gy)
{
    const uint32_t y = gy & 0xFF;
    const uint32_t x = gx & 0xFF;
    return (static_cast<uint32_t>(vPage) << 14) | ((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2) | (x >> 3);
}

uint32_t ZxAttrAddress(uint8_t vPage, uint32_t gx, uint32_t gy)
{
    return (static_cast<uint32_t>(vPage) << 14) + 0x1800 + ((gy & 0xFF) >> 3) * 32 + ((gx & 0xFF) >> 3);
}

/// The TS window of the frame: the graphics window, or all 360x288 with T_CONFIG[0]
const TsConfGeometry::Window& TsuWindow(const TsConfVideoView& v)
{
    return (v.ts->regs[TsConfReg::TConfig] & 0x01) ? TsConfGeometry::kWindows[3] : TsConfGeometry::WindowOf(CurrentVConfig(v));
}

const char* TsuLayerName(TsConfTsu::Layer layer)
{
    switch (layer)
    {
        case TsConfTsu::Layer::S0: return "tsu.s0";
        case TsConfTsu::Layer::T0: return "tsu.t0";
        case TsConfTsu::Layer::S1: return "tsu.s1";
        case TsConfTsu::Layer::T1: return "tsu.t1";
        case TsConfTsu::Layer::S2: return "tsu.s2";
        default:                   return "tsu";
    }
}

/// The sources of a probed TSU pixel: map word + graphics byte, or the three SFILE words + graphics byte
void TsuSources(const TsConfTsu::Source& src, std::vector<SourceRef>& out)
{
    const uint16_t nibble = static_cast<uint16_t>(src.lowNibble ? 0x0F : 0xF0);
    const bool tile = src.layer == TsConfTsu::Layer::T0 || src.layer == TsConfTsu::Layer::T1;
    if (tile)
    {
        out.push_back(RamByte(src.mapAddress, 0xFFFF, SourceRole::TileDescriptor));
        out.back().width = 2;
        out.push_back(RamByte(src.graphicAddress, nibble, SourceRole::TileGraphic));
        return;
    }
    for (uint32_t word = 0; word < 3; word++)
        out.push_back({Space::SpriteRam, 0, 0, (src.descriptor * 3u + word) * 2u, 2, 0xFFFF, SourceRole::SpriteDescriptor});
    out.push_back(RamByte(src.graphicAddress, nibble, SourceRole::SpriteGraphic));
}
} // namespace

bool TsConfVideoMapper::HasTsu(const TsConfState& ts)
{
    return (ts.regs[TsConfReg::TConfig] & 0xE0) != 0;
}

const char* TsConfVideoMapper::LayerId(uint8_t vConfig)
{
    switch (vConfig & 0x03)
    {
        case 0:  return "tszx";
        case 1:  return "ts16";
        case 2:  return "ts256";
        default: return "tstx";
    }
}

const TsConfLine* TsConfVideoMapper::LineOf(const TsConfVideoView& v, uint8_t vConfig, uint32_t y)
{
    const TsConfGeometry::Window& win = TsConfGeometry::WindowOf(vConfig);
    if (y >= win.h)
        return nullptr;
    return &v.engine->Line(win.y0 + y);
}

VideoLayout TsConfVideoMapper::Layout(const VideoState& s) const
{
    VideoLayout layout;
    layout.family = Family();
    layout.mode = s.mode;
    layout.tstatesPerLine = static_cast<uint16_t>(TsConfGeometry::kLineTacts);
    layout.lines = static_cast<uint16_t>(TsConfGeometry::kLines);
    const TsConfVideoView* v = ViewOf(s);
    if (!v)
        return layout;

    const uint8_t vConfig = CurrentVConfig(*v);
    const TsConfGeometry::Window& win = TsConfGeometry::WindowOf(vConfig);
    layout.mapped = true;

    LayerDesc layer;
    layer.id = LayerId(vConfig);
    static constexpr uint8_t kBpp[4] = {1, 4, 8, 1};
    layer.surface = {static_cast<uint16_t>(win.w * 2), win.h, kBpp[vConfig & 0x03]};
    if (IsText(vConfig))
    {
        layer.surface.textColumns = static_cast<uint16_t>(win.w * 2 / 8);
        layer.surface.textRows = static_cast<uint16_t>(win.h / 8);
    }
    layer.window.firstLine = win.y0;
    layer.window.lineCount = win.h;
    layer.window.firstT = static_cast<uint16_t>(win.x0 / 2);
    layer.window.tCount = static_cast<uint16_t>(win.w / 2);
    layer.window.dotsPerT = 4;  // 14 MHz pixels: the framebuffer's unit in every TS mode
    layout.layers.push_back(layer);

    if (HasTsu(*v->ts))
    {
        const TsConfGeometry::Window& ts = TsuWindow(*v);
        LayerDesc tsu;
        tsu.id = "tsu";
        tsu.surface = {static_cast<uint16_t>(ts.w * 2), ts.h, 4};
        tsu.window = {ts.y0, ts.h, static_cast<uint16_t>(ts.x0 / 2), static_cast<uint16_t>(ts.w / 2), 4};
        tsu.ownFramebufferOrigin = true;
        tsu.fbLeft = static_cast<uint16_t>((ts.x0 - kFirstVisibleDot) * 2);
        tsu.fbTop = static_cast<uint16_t>(ts.y0 - kFirstVisibleLine);
        layout.layers.push_back(tsu);
    }

    layout.fb = {static_cast<uint16_t>(ScreenTSConf::kVisibleDots * 2), static_cast<uint16_t>(ScreenTSConf::kVisibleLines),
                 static_cast<uint16_t>((win.x0 - kFirstVisibleDot) * 2), static_cast<uint16_t>(win.y0 - kFirstVisibleLine)};
    return layout;
}

bool TsConfVideoMapper::SourcesAt(const VideoState& s, const MemView& m, size_t layerIndex, uint32_t x, uint32_t y,
                                  LayerContribution& out) const
{
    const TsConfVideoView* v = ViewOf(s);
    if (!v)
        return false;
    if (layerIndex == 1)
    {
        // TSU: the line drawn again with each pixel's source
        if (!HasTsu(*v->ts))
            return false;
        const TsConfGeometry::Window& ts = TsuWindow(*v);
        if (x >= static_cast<uint32_t>(ts.w) * 2u || y >= ts.h)
            return false;
        uint8_t indices[TsConfTsu::kMaxWidth];
        TsConfTsu::Source sources[TsConfTsu::kMaxWidth];
        if (!v->engine->ProbeTsuLine(ts.y0 + y, indices, sources))
            return false;
        const uint32_t dot = x / 2;
        if (!(indices[dot] & 0x0F) || sources[dot].layer == TsConfTsu::Layer::None)
            return false;  // transparent
        out.layer = TsuLayerName(sources[dot].layer);
        out.colourIndex = indices[dot];
        out.sources.clear();
        TsuSources(sources[dot], out.sources);
        out.sources.push_back(CramCell(out.colourIndex));
        out.rgb = ScreenTSConf::CramToRgba(v->ts->cram[out.colourIndex], v->vdac);
        return true;
    }
    if (layerIndex != 0)
        return false;
    const uint8_t frameVConfig = CurrentVConfig(*v);
    const TsConfGeometry::Window& win = TsConfGeometry::WindowOf(frameVConfig);
    if (x >= static_cast<uint32_t>(win.w) * 2u)
        return false;
    const TsConfLine* set = LineOf(*v, frameVConfig, y);
    if (!set)
        return false;

    const uint8_t palBank = static_cast<uint8_t>((set->palSel & 0x0F) << 4);
    const uint32_t dot = x / 2;
    const uint32_t gx = (dot + set->gxOffs) & 0x1FF;
    const uint32_t gy = set->cntRow & 0x1FF;
    const uint8_t vPage = set->vPage;
    out.layer = LayerId(frameVConfig);

    switch (set->vConfig & 0x03)
    {
        case 0:  // ZX
        {
            const uint16_t bit = static_cast<uint16_t>(0x80 >> (gx & 7));
            const SourceRef pixel = RamByte(ZxPixelAddress(vPage, gx, gy), bit, SourceRole::PixelBits);
            SourceRef attr = RamByte(ZxAttrAddress(vPage, gx, gy), 0xFF, SourceRole::Attribute);
            const uint8_t a = m.Read(attr);
            bool ink = (m.Read(pixel) & bit) != 0;
            if ((a & 0x80) && v->state && ((v->state->frame_counter >> 4) & 1))
                ink = !ink;  // flash every 16 frames (hs §4.1)
            attr.bitMask = static_cast<uint16_t>(ink ? 0xC7 : 0xF8);
            out.colourIndex = static_cast<uint8_t>(palBank | ((a & 0x40) ? 0x08 : 0x00) | (ink ? (a & 0x07) : ((a >> 3) & 0x07)));
            out.sources = {pixel, attr, CramCell(out.colourIndex)};
            break;
        }
        case 1:  // 16C
        {
            const uint32_t address = ((static_cast<uint32_t>(vPage) & 0xF8) << 14) | (gy << 8) | (gx >> 1);
            const bool low = gx & 1;
            const SourceRef byte = RamByte(address, static_cast<uint16_t>(low ? 0x0F : 0xF0), SourceRole::PixelBits);
            const uint8_t b = m.Read(byte);
            out.colourIndex = static_cast<uint8_t>(palBank | (low ? (b & 0x0F) : (b >> 4)));
            out.sources = {byte, CramCell(out.colourIndex)};
            break;
        }
        case 2:  // 256C
        {
            const uint32_t address = ((static_cast<uint32_t>(vPage) & 0xF0) << 14) | (gy << 9) | gx;
            const SourceRef byte = RamByte(address, 0xFF, SourceRole::PixelBits);
            out.colourIndex = m.Read(byte);
            out.sources = {byte, CramCell(out.colourIndex)};
            break;
        }
        default:  // TXT: 14 MHz pixels, the character cell every 8
        {
            const uint32_t px = (gx * 2 + (x & 1)) & 0x3FF;
            const uint32_t column = (px >> 3) & 0x7F;
            const uint32_t row = (static_cast<uint32_t>(vPage) << 14) + ((gy >> 3) & 0x3F) * 256;
            const SourceRef code = RamByte(row + column, 0xFF, SourceRole::CharCode);
            SourceRef attr = RamByte(row + 128 + column, 0xFF, SourceRole::CharAttr);
            const uint8_t c = m.Read(code);
            const uint16_t bit = static_cast<uint16_t>(0x80 >> (px & 7));
            const SourceRef font = RamByte((static_cast<uint32_t>(vPage ^ 1) << 14) + c * 8u + (gy & 7), bit, SourceRole::FontRow);
            const bool on = (m.Read(font) & bit) != 0;
            const uint8_t a = m.Read(attr);
            attr.bitMask = static_cast<uint16_t>(on ? 0x0F : 0xF0);
            out.colourIndex = static_cast<uint8_t>(palBank | (on ? (a & 0x0F) : (a >> 4)));
            out.sources = {code, attr, font, CramCell(out.colourIndex)};
            break;
        }
    }
    out.rgb = ScreenTSConf::CramToRgba(v->ts->cram[out.colourIndex], v->vdac);
    return true;
}

void TsConfVideoMapper::BorderSources(const VideoState& s, LayerContribution& out) const
{
    const TsConfVideoView* v = ViewOf(s);
    if (!v)
        return;
    out.layer = "border";
    // BORDER is TS register #0F (a CRAM index; #FE writes set it with PAL_SEL, hs §4.3); TXT shows it as
    // {PAL_SEL[3:0], BORDER[3:0]} ([V] video_render.v, video_out.v; ScreenTSConf draws it so)
    const uint8_t border = v->ts->regs[TsConfReg::Border];
    out.colourIndex = IsText(CurrentVConfig(*v))
                          ? static_cast<uint8_t>(((v->ts->regs[TsConfReg::PalSel] & 0x0F) << 4) | (border & 0x0F))
                          : border;
    out.sources = {{Space::Register, 0, 0x0FAF, 0, 1, 0xFF, SourceRole::Border}, CramCell(out.colourIndex)};
    out.rgb = ScreenTSConf::CramToRgba(v->ts->cram[out.colourIndex], v->vdac);
}

void TsConfVideoMapper::TsuPixelsFor(const TsConfVideoView& v, const SourceRef& ref, std::vector<SurfaceArea>& out)
{
    // A tilemap word, a graphics byte or an SFILE word: every TSU pixel the probe names it for
    if (!HasTsu(*v.ts))
        return;
    const bool sfile = ref.space == Space::SpriteRam;
    const bool cram = ref.space == Space::Palette;
    if (!sfile && !cram && (ref.space != Space::Ram || ref.offset >= 0x4000))
        return;
    const uint32_t physical = (static_cast<uint32_t>(ref.page) << 14) | ref.offset;
    const TsConfGeometry::Window& ts = TsuWindow(v);
    uint8_t indices[TsConfTsu::kMaxWidth];
    TsConfTsu::Source sources[TsConfTsu::kMaxWidth];
    for (uint32_t y = 0; y < ts.h; y++)
    {
        if (!v.engine->ProbeTsuLine(ts.y0 + y, indices, sources))
            continue;
        uint32_t runStart = 0, runLength = 0;
        auto flush = [&]() {
            if (runLength)
                out.push_back({"tsu", static_cast<uint16_t>(runStart * 2), static_cast<uint16_t>(y),
                               static_cast<uint16_t>(runLength * 2), 1});
            runLength = 0;
        };
        for (uint32_t dot = 0; dot < ts.w; dot++)
        {
            const TsConfTsu::Source& src = sources[dot];
            bool hit = false;
            if (src.layer != TsConfTsu::Layer::None && (indices[dot] & 0x0F))
            {
                const bool tile = src.layer == TsConfTsu::Layer::T0 || src.layer == TsConfTsu::Layer::T1;
                if (cram)
                    hit = indices[dot] == ref.offset / 2u;
                else if (sfile)
                    hit = !tile && ref.offset / 6u == src.descriptor;
                else
                    hit = src.graphicAddress == physical ||
                          (tile && (src.mapAddress == physical || src.mapAddress + 1 == physical));
            }
            if (hit && runLength && dot == runStart + runLength)
                runLength++;
            else if (hit)
            {
                flush();
                runStart = dot;
                runLength = 1;
            }
        }
        flush();
    }
}

uint8_t TsConfVideoMapper::GraphicsIndex(const TsConfVideoView& v, const TsConfLine& set, uint32_t x)
{
    // The colour index SourcesAt reports for surface x of a line drawn with `set`
    const uint8_t* ram = v.ram;
    const uint8_t palBank = static_cast<uint8_t>((set.palSel & 0x0F) << 4);
    const uint32_t gx = (x / 2 + set.gxOffs) & 0x1FF;
    const uint32_t gy = set.cntRow & 0x1FF;
    const uint8_t vPage = set.vPage;
    switch (set.vConfig & 0x03)
    {
        case 0:
        {
            const uint8_t a = ram[ZxAttrAddress(vPage, gx, gy)];
            bool ink = (ram[ZxPixelAddress(vPage, gx, gy)] >> (7 - (gx & 7))) & 1;
            if ((a & 0x80) && v.state && ((v.state->frame_counter >> 4) & 1))
                ink = !ink;
            return static_cast<uint8_t>(palBank | ((a & 0x40) ? 0x08 : 0x00) | (ink ? (a & 0x07) : ((a >> 3) & 0x07)));
        }
        case 1:
        {
            const uint8_t b = ram[((static_cast<uint32_t>(vPage) & 0xF8) << 14) | (gy << 8) | (gx >> 1)];
            return static_cast<uint8_t>(palBank | ((gx & 1) ? (b & 0x0F) : (b >> 4)));
        }
        case 2:
            return ram[((static_cast<uint32_t>(vPage) & 0xF0) << 14) | (gy << 9) | gx];
        default:
        {
            const uint32_t px = (gx * 2 + (x & 1)) & 0x3FF;
            const uint32_t row = (static_cast<uint32_t>(vPage) << 14) + ((gy >> 3) & 0x3F) * 256;
            const uint32_t column = (px >> 3) & 0x7F;
            const uint8_t code = ram[row + column];
            const uint8_t a = ram[row + 128 + column];
            const bool on = (ram[(static_cast<uint32_t>(vPage ^ 1) << 14) + code * 8u + (gy & 7)] >> (7 - (px & 7))) & 1;
            return static_cast<uint8_t>(palBank | (on ? (a & 0x0F) : (a >> 4)));
        }
    }
}

void TsConfVideoMapper::PixelsFor(const VideoState& s, const SourceRef& ref, std::vector<SurfaceArea>& out) const
{
    const TsConfVideoView* v = ViewOf(s);
    if (!v)
        return;
    if (ref.space == Space::Palette)
    {
        // A CRAM cell: every graphics pixel and TSU pixel drawn with that index
        if (!v->ram || ref.offset >= 512)
            return;
        const uint8_t index = static_cast<uint8_t>(ref.offset / 2);
        const uint8_t frameVConfig = CurrentVConfig(*v);
        const TsConfGeometry::Window& win = TsConfGeometry::WindowOf(frameVConfig);
        const char* id = LayerId(frameVConfig);
        for (uint32_t y = 0; y < win.h; y++)
        {
            const TsConfLine& set = v->engine->Line(win.y0 + y);
            uint32_t runStart = 0, runLength = 0;
            for (uint32_t x = 0; x <= static_cast<uint32_t>(win.w) * 2u; x++)
            {
                const bool hit = x < static_cast<uint32_t>(win.w) * 2u && GraphicsIndex(*v, set, x) == index;
                if (hit && runLength && x == runStart + runLength)
                {
                    runLength++;
                    continue;
                }
                if (runLength)
                    out.push_back({id, static_cast<uint16_t>(runStart), static_cast<uint16_t>(y), static_cast<uint16_t>(runLength), 1});
                runLength = 0;
                if (hit)
                {
                    runStart = x;
                    runLength = 1;
                }
            }
        }
        TsuPixelsFor(*v, ref, out);
        return;
    }
    TsuPixelsFor(*v, ref, out);
    if (ref.space != Space::Ram || ref.offset >= 0x4000)
        return;
    const uint32_t physical = (static_cast<uint32_t>(ref.page) << 14) | ref.offset;
    const uint8_t frameVConfig = CurrentVConfig(*v);
    const TsConfGeometry::Window& win = TsConfGeometry::WindowOf(frameVConfig);
    const char* id = LayerId(frameVConfig);

    // Line by line with each line's own registers; one area per run of dots
    for (uint32_t y = 0; y < win.h; y++)
    {
        const TsConfLine& set = v->engine->Line(win.y0 + y);
        const uint32_t gy = set.cntRow & 0x1FF;
        const uint8_t vPage = set.vPage;
        auto dotsOf = [&](uint32_t gxFirst, uint32_t count, uint32_t period) {
            // Graphics x gxFirst .. + count - 1 (repeated every `period` within 512, 0 = once) -> window
            // dots; a run can straddle the 512 wrap of the scroll, so dots are placed one by one
            for (uint32_t base = gxFirst; base < 512; base += (period ? period : 512))
            {
                uint32_t runStart = 0, runLength = 0;
                for (uint32_t i = 0; i < count; i++)
                {
                    const uint32_t dot = (base + i - set.gxOffs) & 0x1FF;
                    if (dot >= win.w)
                        continue;
                    if (runLength && dot == runStart + runLength)
                    {
                        runLength++;
                        continue;
                    }
                    if (runLength)
                        out.push_back({id, static_cast<uint16_t>(runStart * 2), static_cast<uint16_t>(y),
                                       static_cast<uint16_t>(runLength * 2), 1});
                    runStart = dot;
                    runLength = 1;
                }
                if (runLength)
                    out.push_back({id, static_cast<uint16_t>(runStart * 2), static_cast<uint16_t>(y),
                                   static_cast<uint16_t>(runLength * 2), 1});
            }
        };
        switch (set.vConfig & 0x03)
        {
            case 0:  // ZX: a pixel byte is 8 dots of one row, an attribute byte 8 dots of 8 rows; columns wrap at 256
            {
                // Rows 192..255 (a Y offset) reach past #1800: pixel bytes up to #1FFF, attributes up to #1BFF
                const uint32_t start = static_cast<uint32_t>(vPage) << 14;
                if (physical < start || physical >= start + 0x2000)
                    break;
                for (uint32_t col = 0; col < 32; col++)
                {
                    const uint32_t gx = col * 8;
                    if (ZxPixelAddress(vPage, gx, gy) == physical || ZxAttrAddress(vPage, gx, gy) == physical)
                        dotsOf(gx, 8, 256);
                }
                break;
            }
            case 1:  // 16C: one byte = 2 dots
            {
                const uint32_t base = (static_cast<uint32_t>(vPage) & 0xF8) << 14;
                if (physical < base || ((physical - base) >> 8) != gy)
                    break;
                dotsOf(((physical - base) & 0xFF) * 2, 2, 0);
                break;
            }
            case 2:  // 256C: one byte = 1 dot
            {
                const uint32_t base = (static_cast<uint32_t>(vPage) & 0xF0) << 14;
                if (physical < base || ((physical - base) >> 9) != gy)
                    break;
                dotsOf((physical - base) & 0x1FF, 1, 0);
                break;
            }
            default:  // TXT: a code / attribute byte feeds one cell (8 px of this row), a font byte every cell with that code
            {
                const uint32_t row = (static_cast<uint32_t>(vPage) << 14) + ((gy >> 3) & 0x3F) * 256;
                const uint32_t font = static_cast<uint32_t>(vPage ^ 1) << 14;
                for (uint32_t column = 0; column < 128; column++)
                {
                    const bool cell = physical == row + column || physical == row + 128 + column;
                    const bool glyph = physical >= font && physical < font + 0x800 &&
                                       v->ram && (physical - font) == 8u * v->ram[row + column] + (gy & 7);
                    if (!cell && !glyph)
                        continue;
                    // Character column -> graphics x (4 dots per cell) -> window
                    dotsOf(column * 4, 4, 0);
                }
                break;
            }
        }
    }
}

bool TsConfVideoMapper::TextAt(const VideoState& s, const MemView& m, size_t layerIndex, uint32_t col, uint32_t row,
                               TextCell& out) const
{
    const TsConfVideoView* v = ViewOf(s);
    if (!v || layerIndex != 0)
        return false;
    const uint8_t vConfig = CurrentVConfig(*v);
    const TsConfGeometry::Window& win = TsConfGeometry::WindowOf(vConfig);
    if (!IsText(vConfig) || col >= static_cast<uint32_t>(win.w) * 2u / 8u || row >= win.h / 8u)
        return false;
    // The cell's first pixel line decides its row and scroll
    const TsConfLine* set = LineOf(*v, vConfig, row * 8);
    if (!set)
        return false;
    const uint32_t gx = (col * 4 + set->gxOffs) & 0x1FF;
    const uint32_t column = ((gx * 2) >> 3) & 0x7F;
    const uint32_t base = (static_cast<uint32_t>(set->vPage) << 14) + (((set->cntRow & 0x1FF) >> 3) & 0x3F) * 256;
    out.codeSource = RamByte(base + column, 0xFF, SourceRole::CharCode);
    out.attrSource = RamByte(base + 128 + column, 0xFF, SourceRole::CharAttr);
    out.code = m.Read(out.codeSource);
    out.attr = m.Read(out.attrSource);
    return true;
}
