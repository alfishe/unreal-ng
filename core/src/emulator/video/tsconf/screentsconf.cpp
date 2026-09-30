#include "screentsconf.h"

#include <algorithm>
#include <cstring>

#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platforms/tsconf/tsconfengine.h"
#include "emulator/platforms/tsconf/tsconfgeometry.h"
#include "emulator/platforms/tsconf/tsconfstate.h"
#include "emulator/ports/models/portdecoder_tsconf.h"

namespace
{
    /// No-VDAC build: the top 2 bits of a 5-bit channel drive a 2-bit DAC, the
    /// low 3 bits a PWM that adds one level on average (not above level 3),
    /// shown as the time average (hs §4.3)
    struct PwmLevels
    {
        uint8_t level[32];
        constexpr PwmLevels() : level{}
        {
            for (int v = 0; v < 32; v++)
            {
                const int dac = v >> 3;
                const int boost = dac < 3 ? (v & 7) : 0;  // eighths of a level
                level[v] = static_cast<uint8_t>((dac * 8 + boost) * 255 / 24);
            }
        }
    };
    constexpr PwmLevels kPwm;
}

ScreenTSConf::ScreenTSConf(EmulatorContext* context) : ScreenZX(context)
{
}

uint32_t ScreenTSConf::CramToRgba(uint16_t cram, uint8_t vdac)
{
    uint32_t level[3] = {static_cast<uint32_t>((cram >> 10) & 0x1F), static_cast<uint32_t>((cram >> 5) & 0x1F),
                         static_cast<uint32_t>(cram & 0x1F)};
    for (uint32_t& v : level)
    {
        if (vdac == 0)
            v = kPwm.level[v];                  // no VDAC: 2-bit DAC + PWM, time-averaged
        else if (!(cram & 0x8000))
            v = v >= 24 ? 255 : v * 255 / 24;   // PWM-compatible linear curve (hs §4.3)
        else
        {
            // The DAC's bits of the channel, its full scale = 255 ([U] tsconf.cpp
            // keeps Ccccc000 for 5 bits; the bit replication here makes 31 white)
            const uint32_t bits = vdac == 1 ? 3u : (vdac == 2 ? 4u : 5u);
            const uint32_t code = v >> (5 - bits);
            v = code * 255 / ((1u << bits) - 1);
        }
    }
    return 0xFF000000u | (level[2] << 16) | (level[1] << 8) | level[0];
}

VideoModeEnum ScreenTSConf::ModeOf(uint8_t vConfig)
{
    static constexpr VideoModeEnum kModes[4] = {M_TSZX, M_TS16, M_TS256, M_TSTX};
    return kModes[vConfig & 0x03];
}

const TsConfState* ScreenTSConf::State()
{
    if (!_ts && _context)
    {
        if (auto* decoder = dynamic_cast<PortDecoder_TSConf*>(_context->pPortDecoder))
        {
            _ts = &decoder->GetState();
            _engine = &decoder->GetEngine();
            _decoder = decoder;
        }
    }
    return _ts;
}

void ScreenTSConf::InitRaster()
{
    const TsConfState* ts = State();
    const VideoModeEnum mode = ts ? ModeOf(ts->regs[TsConfReg::VConfig]) : M_TSZX;

    _vid.mode = mode;
    _vid.raster = raster[R_360_288];
    if (mode != _mode)
        SetVideoMode(mode);
}

ScreenState ScreenTSConf::DescribeScreenState() const
{
    ScreenState s = Screen::DescribeScreenState();
    auto* decoder = _context ? dynamic_cast<PortDecoder_TSConf*>(_context->pPortDecoder) : nullptr;
    if (!decoder)
        return s;

    const TsConfState& ts = decoder->GetState();
    const uint8_t vConfig = ts.regs[TsConfReg::VConfig];
    const uint8_t vPage = ts.regs[TsConfReg::VPage];
    const TsConfGeometry::Window& win = TsConfGeometry::WindowOf(vConfig);
    const uint8_t mode = vConfig & 0x03;

    s.width = win.w;
    s.height = win.h;
    s.videoMode = GetVideoModeName(ModeOf(vConfig)) + " " + std::to_string(win.w) + "x" + std::to_string(win.h);
    s.activeRamPage = vPage;
    s.activeScreen = vPage == 7 ? 1 : 0;
    s.activeRamPages.clear();

    VideoModeInfo f;
    switch (mode)
    {
        case 0:  // ZX layout at V_PAGE
            f = GetVideoModeInfo(M_ZX48);
            s.activeRamPages = {vPage};
            break;
        case 1:  // 16C: 512 x 512 at 4 bpp = 128 KB from V_PAGE & #F8
            f.colorDepth = "4 bpp (16 colors per pixel, PAL_SEL bank)";
            f.colors = 16;
            f.bpp = 4;
            f.attributeSize = "per pixel";
            f.pixelDataBytes = 512u * 512u / 2u;
            f.totalBytes = f.pixelDataBytes;
            for (uint16_t p = 0; p < 8; p++)
                s.activeRamPages.push_back(static_cast<uint16_t>((vPage & 0xF8) + p));
            break;
        case 2:  // 256C: 512 x 512 at 8 bpp = 256 KB from V_PAGE & #F0
            f.colorDepth = "8 bpp (256 colors per pixel)";
            f.colors = 256;
            f.bpp = 8;
            f.attributeSize = "per pixel";
            f.pixelDataBytes = 512u * 512u;
            f.totalBytes = f.pixelDataBytes;
            for (uint16_t p = 0; p < 16; p++)
                s.activeRamPages.push_back(static_cast<uint16_t>((vPage & 0xF0) + p));
            break;
        default:  // TXT: character / attribute rows at V_PAGE, font at V_PAGE ^ 1, 14 MHz pixels
            f.colorDepth = "text, 16-color ink/paper per character (PAL_SEL bank)";
            f.colors = 16;
            f.attributeSize = "8x8 pixels (1 character cell)";
            f.textColumns = static_cast<uint8_t>(win.w * 2 / 8);
            f.textRows = static_cast<uint8_t>(win.h / 8);
            s.width = static_cast<uint16_t>(win.w * 2);
            s.activeRamPages = {vPage, static_cast<uint16_t>(vPage ^ 1)};
            break;
    }
    s.format = f;
    return s;
}

void ScreenTSConf::DrawTo(uint32_t raster)
{
    if (raster <= _prevTstate)
        return;
    DrawPeriod(_prevTstate, raster);
    _prevTstate = raster;
}

const void* ScreenTSConf::VideoFamilyView() const
{
    const TsConfState* ts = const_cast<ScreenTSConf*>(this)->State();
    if (!ts || !_engine || !_context)
        return nullptr;
    _view.ts = ts;
    _view.engine = _engine;
    _view.state = &_context->emulatorState;
    _view.ram = _context->pMemory ? _context->pMemory->RAMBase() : nullptr;
    _view.vdac = _context->config.ts_vdac;
    return &_view;
}

void ScreenTSConf::SetVideoMode(VideoModeEnum mode)
{
    // The raster state and framebuffer come from the descriptor; the ZX
    // renderer's tables are not used by this screen
    Screen::SetVideoMode(mode);
}

void ScreenTSConf::SetBorderColor(uint8_t color)
{
    // The border is TS BORDER (a CRAM index) - drawn from the state; flush
    // the beam up to the write like every other screen
    Screen::SetBorderColor(color);
}

void ScreenTSConf::SetActiveScreen(SpectrumScreenEnum screen)
{
    UpdateScreen();
    Screen::SetActiveScreen(screen);
}

void ScreenTSConf::RenderFrameBatch()
{
    _paletteVerify = true;
    DrawRange(0, kLineTacts * kLines - 1);
    _paletteVerify = false;
}

void ScreenTSConf::RenderOnlyMainScreen()
{
    _paletteVerify = true;
    DrawRange(0, kLineTacts * kLines - 1);
    _paletteVerify = false;
}

void ScreenTSConf::FillBorderWithColor([[maybe_unused]] uint8_t color)
{
    // The border is part of every frame the range renderer draws
}

void ScreenTSConf::RefreshPalette(const TsConfState& ts)
{
    const uint8_t vdac = _context ? _context->config.ts_vdac : 0;
    if (vdac != _paletteVdac)
    {
        _paletteVdac = vdac;
        _paletteValid = false;
    }
    // The beam renderer calls this every few dots: unchanged CRAM costs one
    // compare of the decoder's CRAM version; the whole-frame renders compare
    // every cell (direct writes into the state do not move the version)
    const uint32_t version = _decoder ? _decoder->CramVersion() : 0;
    if (_paletteValid && !_paletteVerify && _decoder && version == _paletteVersion)
        return;
    _paletteVersion = version;
    if (_paletteValid && std::memcmp(ts.cram, _paletteCram, sizeof(_paletteCram)) == 0)
        return;
    for (uint32_t i = 0; i < 256; i++)
    {
        if (!_paletteValid || ts.cram[i] != _paletteCram[i])
        {
            _paletteCram[i] = ts.cram[i];
            _palette[i] = CramToRgba(ts.cram[i], vdac);
        }
    }
    _paletteValid = true;
}

void ScreenTSConf::GraphicsSpan(const TsConfLine& set, uint32_t wx, uint32_t count, uint8_t* index0, uint8_t* index1,
                                uint8_t* visible0, uint8_t* visible1) const
{
    const uint8_t palBank = static_cast<uint8_t>((set.palSel & 0x0F) << 4);
    const uint32_t gy = set.cntRow & 0x1FF;
    const uint8_t* ram = _context->pMemory->RAMBase();
    const uint32_t vPage = set.vPage;
    uint32_t gx = (wx + set.gxOffs) & 0x1FF;

    // SIMD-CANDIDATE(TS-O2): the 16C / 256C loops are a gather with a 512-dot
    // wrap; 8-dot runs between wraps would vectorize
    switch (set.vConfig & 0x03)
    {
        case 0:  // ZX: Spectrum layout at V_PAGE, rows wrap at 256, columns at 32 bytes
        {
            const uint32_t y = gy & 0xFF;
            const uint8_t* page = ram + (vPage << 14);
            const uint8_t* pixelRow = page + (((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2));
            const uint8_t* attrRow = page + 0x1800 + (y >> 3) * 32;
            const bool flashPhase = (_context->emulatorState.frame_counter >> 4) & 1;  // every 16 frames (hs §4.1)
            for (uint32_t i = 0; i < count; i++, gx = (gx + 1) & 0x1FF)
            {
                const uint32_t x = gx & 0xFF;
                const uint8_t attr = attrRow[x >> 3];
                bool ink = (pixelRow[x >> 3] >> (7 - (x & 7))) & 1;
                if ((attr & 0x80) && flashPhase)
                    ink = !ink;
                visible0[i] = ink;
                const uint8_t bright = (attr & 0x40) ? 0x08 : 0x00;
                index0[i] = static_cast<uint8_t>(palBank | bright | (ink ? (attr & 0x07) : ((attr >> 3) & 0x07)));
            }
            break;
        }
        case 1:  // 16C: 4 bpp, high nibble = left pixel
        {
            const uint8_t* row = ram + ((vPage & 0xF8) << 14) + (gy << 8);
            for (uint32_t i = 0; i < count; i++, gx = (gx + 1) & 0x1FF)
            {
                const uint8_t byte = row[gx >> 1];
                const uint8_t nibble = (gx & 1) ? (byte & 0x0F) : (byte >> 4);
                visible0[i] = nibble != 0;
                index0[i] = static_cast<uint8_t>(palBank | nibble);
            }
            break;
        }
        case 2:  // 256C: 8 bpp
        {
            const uint8_t* row = ram + ((vPage & 0xF0) << 14) + (gy << 9);
            for (uint32_t i = 0; i < count; i++, gx = (gx + 1) & 0x1FF)
            {
                const uint8_t pixel = row[gx];
                visible0[i] = pixel != 0;
                index0[i] = pixel;
            }
            break;
        }
        default:  // TXT: 256-byte rows (128 characters, 128 attributes) at V_PAGE, font at V_PAGE ^ 1
        {
            const uint8_t* row = ram + (vPage << 14) + ((gy >> 3) & 0x3F) * 256;
            const uint8_t* font = ram + ((vPage ^ 1) << 14) + (gy & 7);
            // The character cell changes every 8 pixels: its font byte and
            // colors are looked up once per cell
            uint32_t cell = ~0u;
            uint8_t glyph = 0;
            uint8_t ink = 0;
            uint8_t paper = 0;
            auto pixel = [&](uint32_t px, uint8_t& index, uint8_t& visible) {
                const uint32_t column = (px >> 3) & 0x7F;
                if (column != cell)
                {
                    cell = column;
                    const uint8_t attr = row[128 + column];
                    glyph = font[row[column] * 8];
                    ink = static_cast<uint8_t>(palBank | (attr & 0x0F));
                    paper = static_cast<uint8_t>(palBank | (attr >> 4));
                }
                const bool on = (glyph >> (7 - (px & 7))) & 1;
                visible = on;
                index = on ? ink : paper;
            };
            for (uint32_t i = 0; i < count; i++, gx = (gx + 1) & 0x1FF)
            {
                pixel((gx * 2) & 0x3FF, index0[i], visible0[i]);
                pixel((gx * 2 + 1) & 0x3FF, index1[i], visible1[i]);
            }
            break;
        }
    }
}

bool ScreenTSConf::DirectSpan(const TsConfState& ts, const TsConfLine& set, uint32_t line, uint32_t dotFrom,
                              uint32_t dotTo, uint32_t* out) const
{
    const uint8_t vConfig = set.vConfig;
    if ((set.tsu && !(vConfig & 0x10)) || (vConfig & 0x08))
        return false;  // TSU pixels to mix, or GFXOVR (invisible dots show the border)

    const bool text = (vConfig & 0x03) == 3;
    const uint8_t palBank = static_cast<uint8_t>((set.palSel & 0x0F) << 4);
    const uint8_t border = ts.regs[TsConfReg::Border];
    const uint32_t borderRgb = _palette[text ? static_cast<uint8_t>(palBank | (border & 0x0F)) : border];
    const TsConfGeometry::Window& win = TsConfGeometry::WindowOf(vConfig);

    uint32_t gfxFrom = dotTo;
    uint32_t gfxTo = dotTo;
    if (!(vConfig & 0x20) && line >= win.y0 && line < static_cast<uint32_t>(win.y0 + win.h))
    {
        gfxFrom = std::max<uint32_t>(dotFrom, win.x0);
        gfxTo = std::min<uint32_t>(dotTo, static_cast<uint32_t>(win.x0 + win.w));
        if (gfxFrom >= gfxTo)
            gfxFrom = gfxTo = dotTo;
    }

    for (uint32_t dot = dotFrom; dot < gfxFrom; dot++, out += 2)
        out[0] = out[1] = borderRgb;

    const uint32_t count = gfxTo - gfxFrom;
    if (count)
    {
        const uint32_t gy = set.cntRow & 0x1FF;
        const uint8_t* ram = _context->pMemory->RAMBase();
        const uint32_t vPage = set.vPage;
        uint32_t gx = (gfxFrom - win.x0 + set.gxOffs) & 0x1FF;
        switch (vConfig & 0x03)
        {
            case 0:  // ZX: ink / paper colours once per 8-dot cell
            {
                const uint32_t y = gy & 0xFF;
                const uint8_t* page = ram + (vPage << 14);
                const uint8_t* pixelRow = page + (((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2));
                const uint8_t* attrRow = page + 0x1800 + (y >> 3) * 32;
                const bool flashPhase = (_context->emulatorState.frame_counter >> 4) & 1;
                uint32_t cell = ~0u;
                uint8_t pixels = 0;
                uint32_t ink = 0, paper = 0;
                for (uint32_t i = 0; i < count; i++, gx = (gx + 1) & 0x1FF, out += 2)
                {
                    const uint32_t x = gx & 0xFF;
                    if ((x >> 3) != cell)
                    {
                        cell = x >> 3;
                        const uint8_t attr = attrRow[cell];
                        pixels = pixelRow[cell];
                        if ((attr & 0x80) && flashPhase)
                            pixels = static_cast<uint8_t>(~pixels);
                        const uint8_t bright = (attr & 0x40) ? 0x08 : 0x00;
                        ink = _palette[palBank | bright | (attr & 0x07)];
                        paper = _palette[palBank | bright | ((attr >> 3) & 0x07)];
                    }
                    out[0] = out[1] = ((pixels >> (7 - (x & 7))) & 1) ? ink : paper;
                }
                break;
            }
            case 1:  // 16C
            {
                const uint8_t* row = ram + ((vPage & 0xF8) << 14) + (gy << 8);
                for (uint32_t i = 0; i < count; i++, gx = (gx + 1) & 0x1FF, out += 2)
                {
                    const uint8_t byte = row[gx >> 1];
                    out[0] = out[1] = _palette[palBank | ((gx & 1) ? (byte & 0x0F) : (byte >> 4))];
                }
                break;
            }
            case 2:  // 256C
            {
                const uint8_t* row = ram + ((vPage & 0xF0) << 14) + (gy << 9);
                for (uint32_t i = 0; i < count; i++, gx = (gx + 1) & 0x1FF, out += 2)
                    out[0] = out[1] = _palette[row[gx]];
                break;
            }
            default:  // TXT: ink / paper colours once per character cell
            {
                const uint8_t* row = ram + (vPage << 14) + ((gy >> 3) & 0x3F) * 256;
                const uint8_t* font = ram + ((vPage ^ 1) << 14) + (gy & 7);
                uint32_t cell = ~0u;
                uint8_t glyph = 0;
                uint32_t ink = 0, paper = 0;
                for (uint32_t i = 0; i < count;)
                {
                    // A whole character (4 dots = 8 pixels) at once where one starts
                    if ((gx & 3) == 0 && count - i >= 4)
                    {
                        const uint32_t column = (gx >> 2) & 0x7F;
                        const uint8_t attr = row[128 + column];
                        const uint8_t bits = font[row[column] * 8];
                        const uint32_t on = _palette[palBank | (attr & 0x0F)];
                        const uint32_t off = _palette[palBank | (attr >> 4)];
                        for (uint32_t b = 0; b < 8; b++)
                            out[b] = ((bits >> (7 - b)) & 1) ? on : off;
                        out += 8;
                        i += 4;
                        gx = (gx + 4) & 0x1FF;
                        cell = ~0u;
                        continue;
                    }
                    for (uint32_t sub = 0; sub < 2; sub++)
                    {
                        const uint32_t px = (gx * 2 + sub) & 0x3FF;
                        const uint32_t column = (px >> 3) & 0x7F;
                        if (column != cell)
                        {
                            cell = column;
                            const uint8_t attr = row[128 + column];
                            glyph = font[row[column] * 8];
                            ink = _palette[palBank | (attr & 0x0F)];
                            paper = _palette[palBank | (attr >> 4)];
                        }
                        out[sub] = ((glyph >> (7 - (px & 7))) & 1) ? ink : paper;
                    }
                    i++;
                    gx = (gx + 1) & 0x1FF;
                    out += 2;
                }
                break;
            }
        }
    }

    for (uint32_t dot = std::max(gfxTo, dotFrom); dot < dotTo; dot++, out += 2)
        out[0] = out[1] = borderRgb;
    return true;
}

void ScreenTSConf::DrawLineSpan(const TsConfState& ts, uint32_t line, uint32_t dotFrom, uint32_t dotTo, uint32_t* out) const
{
    const TsConfLine& set = _engine->Line(line);
    if (DirectSpan(ts, set, line, dotFrom, dotTo, out))
        return;
    const uint8_t vConfig = set.vConfig;
    const bool text = (vConfig & 0x03) == 3;
    const uint8_t border = ts.regs[TsConfReg::Border];
    const TsConfGeometry::Window& win = TsConfGeometry::WindowOf(vConfig);

    // Color indices of the span, per half dot (the second one only in TXT)
    uint8_t index0[448];
    uint8_t index1[448];
    uint8_t visible0[448];
    uint8_t visible1[448];
    std::memset(index0 + dotFrom, border, dotTo - dotFrom);
    if (text)
        std::memset(index1 + dotFrom, border, dotTo - dotFrom);

    // Graphics inside the window (NOGFX: the window shows what is outside it)
    uint32_t gfxFrom = dotTo;
    uint32_t gfxTo = dotTo;
    if (!(vConfig & 0x20) && line >= win.y0 && line < static_cast<uint32_t>(win.y0 + win.h))
    {
        gfxFrom = std::max<uint32_t>(dotFrom, win.x0);
        gfxTo = std::min<uint32_t>(dotTo, static_cast<uint32_t>(win.x0 + win.w));
        if (gfxFrom < gfxTo)
        {
            GraphicsSpan(set, gfxFrom - win.x0, gfxTo - gfxFrom, index0 + gfxFrom, index1 + gfxFrom, visible0 + gfxFrom,
                         visible1 + gfxFrom);
            // GFXOVR: only "visible" graphics dots stay; the others show what
            // is behind them - the TSU below, else BORDER
            if (vConfig & 0x08)
            {
                for (uint32_t dot = gfxFrom; dot < gfxTo; dot++)
                {
                    if (!visible0[dot])
                        index0[dot] = border;
                    if (text && !visible1[dot])
                        index1[dot] = border;
                }
            }
        }
        else
        {
            gfxFrom = gfxTo = dotTo;
        }
    }

    // TSU (TS-O3: only over the TS window of lines it drew on). Video plex
    // ([V] video_render.v:75-82): inside the graphics window the TSU wins over
    // graphics unless GFXOVR and the graphics dot is "visible"; outside it
    // the TSU shows over the border
    if (set.tsu && !(vConfig & 0x10))  // NOTSU
    {
        const uint32_t from = std::max<uint32_t>(dotFrom, set.tsX0);
        const uint32_t to = std::min<uint32_t>(dotTo, static_cast<uint32_t>(set.tsX0 + set.tsW));
        const uint8_t* tsu = _engine->TsuRow(line) - set.tsX0;
        const bool gfxOver = vConfig & 0x08;
        for (uint32_t dot = from; dot < to; dot++)
        {
            const uint8_t pixel = tsu[dot];
            if (!(pixel & 0x0F))
                continue;
            if (gfxOver && dot >= gfxFrom && dot < gfxTo)
            {
                if (!visible0[dot])
                    index0[dot] = pixel;
                if (text && !visible1[dot])
                    index1[dot] = pixel;
            }
            else
            {
                index0[dot] = pixel;
                index1[dot] = pixel;
            }
        }
    }

    if (text)
    {
        // TXT is hires: every source flattens to 4 bits in the PAL_SEL bank (hs §4.2)
        const uint8_t palBank = static_cast<uint8_t>((set.palSel & 0x0F) << 4);
        for (uint32_t dot = dotFrom; dot < dotTo; dot++, out += 2)
        {
            out[0] = _palette[palBank | (index0[dot] & 0x0F)];
            out[1] = _palette[palBank | (index1[dot] & 0x0F)];
        }
    }
    else
    {
        for (uint32_t dot = dotFrom; dot < dotTo; dot++, out += 2)
            out[0] = out[1] = _palette[index0[dot]];
    }
}

void ScreenTSConf::DrawRange(uint32_t fromTstate, uint32_t toTstate)
{
    const TsConfState* ts = State();
    if (!ts || !_engine || !_framebuffer.memoryBuffer || _framebuffer.width != kVisibleDots * 2 ||
        _framebuffer.height != kVisibleLines)
        return;

    const uint32_t frameEnd = kLineTacts * kLines;
    if (fromTstate >= frameEnd)
        return;
    if (toTstate >= frameEnd)
        toTstate = frameEnd - 1;

    // CRAM as it is now: the colors of the whole range, as the per-dot
    // renderer read them at draw time
    RefreshPalette(*ts);
    uint32_t* fb = reinterpret_cast<uint32_t*>(_framebuffer.memoryBuffer);

    // One span per raster line of the range
    for (uint32_t t = fromTstate; t <= toTstate;)
    {
        const uint32_t line = t / kLineTacts;
        const uint32_t lineStart = line * kLineTacts;
        const uint32_t lastTact = std::min(toTstate - lineStart, kLineTacts - 1);
        const uint32_t firstTact = std::max(t - lineStart, kFirstVisibleTact);
        t = lineStart + kLineTacts;
        if (line < kFirstVisibleLine || firstTact > lastTact)
            continue;

        uint32_t* out = fb + (line - kFirstVisibleLine) * (kVisibleDots * 2) + (firstTact - kFirstVisibleTact) * 4;
        DrawLineSpan(*ts, line, firstTact * 2, (lastTact + 1) * 2, out);
    }
}
